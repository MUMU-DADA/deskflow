/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016, 2025 - 2026 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreArgParser.h"

#include "arch/Arch.h"
#include "base/EventQueue.h"
#include "base/Log.h"
#include "common/Constants.h"
#include "common/ExitCodes.h"
#include "deskflow/ClientApp.h"
#include "deskflow/ServerApp.h"
#include "deskflow/ipc/CoreIpcServer.h"
#include "platform/SystemVolume.h"

#if defined(Q_OS_WIN)
#include "arch/win32/ArchMiscWindows.h"
#elif defined(Q_OS_MACOS)
#include "MacCoreApplication.h"
#endif

#include <QApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSharedMemory>
#include <QTextStream>
#include <QThread>

void qtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
  const auto utf8 = message.toUtf8();
  switch (type) {
  case QtDebugMsg:
    CLOG->print(context.file, context.line, CLOG_TAG_DEBUG "%s", utf8.constData());
    break;
  case QtInfoMsg:
    CLOG->print(context.file, context.line, CLOG_TAG_INFO "%s", utf8.constData());
    break;
  case QtWarningMsg:
    CLOG->print(context.file, context.line, CLOG_TAG_WARN "%s", utf8.constData());
    break;
  case QtCriticalMsg:
    CLOG->print(context.file, context.line, CLOG_TAG_ERR "%s", utf8.constData());
    break;
  case QtFatalMsg:
    CLOG->print(context.file, context.line, CLOG_TAG_CRIT "%s", utf8.constData());
    break;
  }
  if (type == QtFatalMsg) {
    abort();
  }
}

void showHelp(const CoreArgParser &parser)
{
  QTextStream(stdout) << parser.helpText();
}

App *createApp(const CoreArgParser &parser, EventQueue &events, const QString &processName)
{
  if (parser.serverMode()) {
    return new ServerApp(&events, processName);
  } else if (parser.clientMode()) {
    return new ClientApp(&events, processName);
  }
  return nullptr;
}

int main(int argc, char **argv)
{
#if defined(Q_OS_WIN)
  ArchMiscWindows::setInstanceWin32(GetModuleHandle(nullptr));
#elif defined(Q_OS_MACOS)
  deskflow::MacCoreApplicationBootstrap macBootstrap;
  if (!macBootstrap.prepare())
    return s_exitFailed;
#endif

  QApplication::setApplicationName(QStringLiteral("%1 Core").arg(kAppName));
  QApplication app(argc, argv);

#if defined(Q_OS_MACOS)
  if (!macBootstrap.finish())
    return s_exitFailed;
#endif

  Arch arch;
  arch.init();

  Log log;
  qInstallMessageHandler(qtMessageHandler);

  CoreArgParser parser(QCoreApplication::arguments());

  // Print any parser errors
  if (!parser.errorText().isEmpty()) {
    QTextStream(stdout) << parser.errorText() << "\n";
  }

  if (parser.help()) {
    showHelp(parser);
    return s_exitSuccess;
  }

  if (parser.version()) {
    QTextStream(stdout) << parser.versionText();
    return s_exitSuccess;
  }

  // Diagnostics exit before Settings, shared-memory, input hooks and networking.
  // --volume-check deliberately keeps the current volume and mute values.
  if (parser.volumeStatus() || parser.volumeCheck()) {
    if (!parser.errorText().isEmpty())
      return s_exitFailed;
    auto control = deskflow::volume::makeSystemVolumeControl();
    const auto before = control->read();
    QJsonObject result{{"platformSupported", deskflow::volume::systemVolumeSupported()}, {"available", bool(before)}};
    if (before) {
      result.insert("device", before->deviceName);
      result.insert("deviceId", before->deviceId);
      result.insert("volume", before->state.volume / 100.0);
      result.insert("muted", before->state.muted);
      result.insert("volumeSupported", bool(before->capabilities & deskflow::volume::Volume));
      result.insert("muteSupported", bool(before->capabilities & deskflow::volume::Mute));
    }
    bool checked = false;
    if (parser.volumeCheck() && before && (before->capabilities & deskflow::volume::Volume)) {
      checked = control->write(before->deviceId, before->state, before->capabilities);
      const auto after = control->read();
      checked = checked && after && after->deviceId == before->deviceId && after->state == before->state;
      result.insert("unchangedWriteVerified", checked);
      if (after) {
        result.insert("volumeAfter", after->state.volume / 100.0);
        result.insert("mutedAfter", after->state.muted);
      }
    }
    QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << '\n';
    return parser.volumeCheck() && !checked ? s_exitFailed : s_exitSuccess;
  }

  // Before we check any more args we need to check for a duplicate process.
  // Create a shared memory segment with a unique key
  // This is to prevent a new instance from running if one is already running
  QSharedMemory sharedMemory(kCoreBinName);

  // Attempt to attach first and detach in order to clean up stale shm chunks
  // This can happen if the previous instance was killed or crashed
  if (sharedMemory.attach())
    sharedMemory.detach();

  if (!sharedMemory.create(1) && parser.singleInstanceOnly()) {
    LOG_WARN("an instance of deskflow core is already running");
    return s_exitDuplicate;
  }

  parser.parse();

  EventQueue events;
  const auto processName = QFileInfo(argv[0]).fileName();

  App *coreApp = createApp(parser, events, processName);

  const auto ipcServer = new deskflow::core::ipc::CoreIpcServer(&app, &events); // NOSONAR - Qt managed
  QObject::connect(
      ipcServer, &deskflow::core::ipc::IpcServer::stopProcessRequested, coreApp, &App::quit, Qt::DirectConnection
  );
  ipcServer->listen();

  QThread coreThread;
  QObject::connect(&coreThread, &QThread::finished, &app, &QApplication::quit);
  coreApp->run(coreThread);

  int exitCode = QApplication::exec();
  coreThread.wait();

  if (exitCode == s_exitSuccess) {
    exitCode = coreApp->getExitCode();
  }
  delete coreApp;

  LOG_DEBUG("core exited, code: %d", exitCode);
  return exitCode;
}
