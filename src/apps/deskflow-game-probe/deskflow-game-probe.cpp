/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ProbeOptions.h"
#include "ProbeWait.h"
#include "platform/ForegroundGame.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <thread>

namespace {

int fail(const QString &message, int exitCode = 2)
{
  const auto bytes = message.toUtf8();
  std::fprintf(stderr, "deskflow-game-probe: %s\n", bytes.constData());
  return exitCode;
}

bool writeSample(QFile &output, const deskflow::ForegroundGameState &state)
{
  const QJsonObject object{
      {QStringLiteral("supported"), state.supported},
      {QStringLiteral("fullscreen"), state.fullscreen},
      {QStringLiteral("game"), state.game},
      {QStringLiteral("guard"), state.supported && state.fullscreen && state.game},
      {QStringLiteral("application"), state.application},
      {QStringLiteral("detail"), state.detail},
      {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}
  };
  const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
  return output.write(bytes) == bytes.size() && output.flush();
}

} // namespace

int main(int argc, char **argv)
{
  // QCoreApplication creates no GUI and does not activate a window. In particular,
  // do not initialise Settings, AppUtil, logging, input hooks, IPC or any core.
  QCoreApplication application(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("Deskflow Game Probe"));
  QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Read-only foreground fullscreen game detector. Emits newline JSON; does not control input.")
  );
  const auto help = parser.addHelpOption();
  const auto version = parser.addVersionOption();
  const QCommandLineOption gameApp(
      QStringLiteral("game-app"),
      QStringLiteral("Additional exact executable name, path or application ID; repeatable."),
      QStringLiteral("identifier")
  );
  const QCommandLineOption samplesOption(
      QStringLiteral("samples"), QStringLiteral("Number of observations (1-1200)."), QStringLiteral("count"),
      QStringLiteral("1")
  );
  const QCommandLineOption intervalOption(
      QStringLiteral("interval-ms"), QStringLiteral("Interval between observation starts (100-60000 ms)."),
      QStringLiteral("milliseconds"), QStringLiteral("500")
  );
  const QCommandLineOption stateFileOption(
      QStringLiteral("state-file"), QStringLiteral("Optional existing Wayland integration state file."),
      QStringLiteral("path")
  );
  const QCommandLineOption outputOption(
      QStringLiteral("output"),
      QStringLiteral("Write JSON lines to a new file instead of stdout; existing files are not overwritten."),
      QStringLiteral("path")
  );
  parser.addOptions({gameApp, samplesOption, intervalOption, stateFileOption, outputOption});
  if (!parser.parse(application.arguments()))
    return fail(parser.errorText());
  if (parser.isSet(help))
    parser.showHelp(0);
  if (parser.isSet(version))
    parser.showVersion();
  if (!parser.positionalArguments().isEmpty())
    return fail(QStringLiteral("Unexpected positional arguments; use --help for usage"));

  int samples = 0;
  int intervalMs = 0;
  const auto error =
      deskflow::probe::validateSampling(parser.value(samplesOption), parser.value(intervalOption), samples, intervalMs);
  if (!error.isEmpty())
    return fail(error);
  const auto gameApps = parser.values(gameApp);
  for (const auto &identifier : gameApps) {
    if (identifier.trimmed().isEmpty())
      return fail(QStringLiteral("--game-app requires a nonempty exact identifier"));
  }
  const auto stateFile = parser.value(stateFileOption);
  if (parser.isSet(stateFileOption) && stateFile.isEmpty())
    return fail(QStringLiteral("--state-file requires a nonempty path"));

  QString environmentError;
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
  const bool usesX11 = stateFile.isEmpty() && qEnvironmentVariableIsEmpty("SWAYSOCK") &&
                       qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE") &&
                       qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") &&
                       qEnvironmentVariable("XDG_SESSION_TYPE") != QStringLiteral("wayland");
  if (usesX11 && !deskflow::probe::isLocalX11Display(qEnvironmentVariable("DISPLAY")))
    environmentError = QStringLiteral("The probe does not connect to remote X11 displays; use a local desktop session");
#endif

  QFile output;
  if (parser.isSet(outputOption)) {
    output.setFileName(parser.value(outputOption));
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
      return fail(QStringLiteral("Cannot create output file: %1").arg(output.errorString()), 74);
  } else if (!output.open(stdout, QIODevice::WriteOnly, QFileDevice::DontCloseHandle)) {
    return fail(QStringLiteral("Cannot open stdout"), 74);
  }

  using namespace std::chrono;
  const auto started = steady_clock::now();
  const auto deadline = started + milliseconds(deskflow::probe::kMaximumDurationMs);
  for (int index = 0; index < samples; ++index) {
    if (index)
      deskflow::probe::waitUntil(started + milliseconds(static_cast<qint64>(index) * intervalMs), [] { return false; });
    else
      deskflow::probe::pumpMainEvents();
    if (steady_clock::now() >= deadline)
      return fail(QStringLiteral("Sampling stopped at the 15-minute runtime limit"), 75);

    deskflow::ForegroundGameState state;
    bool timedOut = false;
    bool workerRunning = false;
    try {
      // Use the production detector, with immutable explicit inputs. At most
      // one query runs; a stalled native service emits one error then ends the
      // process instead of creating more workers or hanging the caller.
      std::packaged_task<deskflow::ForegroundGameState()> query([gameApps, stateFile, environmentError] {
        if (!environmentError.isEmpty()) {
          deskflow::ForegroundGameState unavailable;
          unavailable.detail = environmentError;
          return unavailable;
        }
        return deskflow::queryForegroundGame(gameApps, stateFile);
      });
      auto result = query.get_future();
      std::thread(std::move(query)).detach();
      workerRunning = true;
      const auto queryDeadline =
          std::min(steady_clock::now() + milliseconds(deskflow::probe::kQueryTimeoutMs), deadline);
      if (deskflow::probe::waitUntil(queryDeadline, [&result] {
            return result.wait_for(milliseconds(0)) == std::future_status::ready;
          })) {
        workerRunning = false;
        state = result.get();
      } else {
        state.detail = QStringLiteral("Native detector timed out after 1000 ms; probe stopped");
        timedOut = true;
      }
    } catch (const std::exception &exception) {
      state.detail = QStringLiteral("Native detector failed: %1").arg(QString::fromUtf8(exception.what()));
      timedOut = true;
    }
    if (!writeSample(output, state)) {
      const auto code = fail(QStringLiteral("Cannot write JSON output: %1").arg(output.errorString()), 74);
      if (workerRunning)
        std::_Exit(code);
      return code;
    }
    if (workerRunning) {
      // Avoid destroying Qt/application statics while a native call remains
      // blocked in another thread. The JSON row is already flushed.
      output.close();
      std::_Exit(75);
    }
    if (timedOut)
      return 75;
  }
  return 0;
}
