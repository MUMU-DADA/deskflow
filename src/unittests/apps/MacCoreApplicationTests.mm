/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MacCoreApplicationTests.h"

#include "MacCoreApplication.h"
#include "common/Constants.h"
#include "common/ExitCodes.h"

#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSharedMemory>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <cstring>

#import <AppKit/AppKit.h>

// The production entry point is compiled unchanged, with only its symbol
// renamed, so help/version/duplicate tests exercise its real return paths.
int deskflowCoreMain(int argc, char **argv);

namespace {
constexpr auto foregroundTransformVariable = "QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM";

int printState(QJsonObject state)
{
  state.insert("policy", NSApp ? static_cast<int>([NSApp activationPolicy]) : -1);
  state.insert("environmentSet", qEnvironmentVariableIsSet(foregroundTransformVariable));
  state.insert("environmentValue", QString::fromUtf8(qgetenv(foregroundTransformVariable)));
  const auto json = QJsonDocument(state).toJson(QJsonDocument::Compact);
  std::printf("%s\n", json.constData());
  return 0;
}

struct ChildResult
{
  bool finished = false;
  int exitCode = -1;
  QByteArray errors;
  QJsonObject state;
};

ChildResult runChild(const QStringList &args, const QString &initialValue = {})
{
  QTemporaryDir isolated;
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert("QT_QPA_PLATFORM", "cocoa"); // CI normally sets offscreen; this test must use native AppKit.
  environment.insert("XDG_CONFIG_HOME", isolated.path() + "/config");
  environment.insert("XDG_STATE_HOME", isolated.path() + "/state");
  environment.remove(foregroundTransformVariable);
  if (!initialValue.isNull())
    environment.insert(foregroundTransformVariable, initialValue);

  QProcess process;
  process.setProcessEnvironment(environment);
  process.start(QCoreApplication::applicationFilePath(), args);
  ChildResult result;
  result.finished = process.waitForFinished(15000);
  if (!result.finished) {
    process.kill();
    process.waitForFinished(1000);
  }
  result.exitCode = process.exitCode();
  result.errors = process.readAllStandardError();
  const auto lastLine = process.readAllStandardOutput().trimmed().split('\n').last();
  result.state = QJsonDocument::fromJson(lastLine).object();
  return result;
}
} // namespace

void MacCoreApplicationTests::qtInitializationStaysBackground_data()
{
  QTest::addColumn<QString>("initialValue");
  QTest::newRow("unset") << QString();
  QTest::newRow("empty") << QStringLiteral("");
  QTest::newRow("existing-value") << QStringLiteral("preserve-this-value");
}

void MacCoreApplicationTests::qtInitializationStaysBackground()
{
  QFETCH(QString, initialValue);
  const auto child = runChild({"--bootstrap-child"}, initialValue);
  QVERIFY2(child.finished, child.errors.constData());
  QCOMPARE(child.exitCode, 0);
  QVERIFY(child.state.value("prepared").toBool());
  // Checking only the final policy misses a transient Regular registration.
  QCOMPARE(child.state.value("beforePolicy").toInt(-1), static_cast<int>(NSApplicationActivationPolicyProhibited));
  QVERIFY(child.state.value("finished").toBool());
  QCOMPARE(child.state.value("policy").toInt(-1), static_cast<int>(NSApplicationActivationPolicyProhibited));
  QVERIFY(child.state.value("clipboardAvailable").toBool());
  QVERIFY(child.state.value("eventDelivered").toBool());
  QCOMPARE(child.state.value("environmentSet").toBool(), !initialValue.isNull());
  QCOMPARE(child.state.value("environmentValue").toString(), initialValue);
}

void MacCoreApplicationTests::unsuppressedQtIsDetectedAndDemoted()
{
  const auto child = runChild({"--qt-control-child"});
  QVERIFY2(child.finished, child.errors.constData());
  QCOMPARE(child.exitCode, 0);
  QCOMPARE(child.state.value("beforePolicy").toInt(-1), static_cast<int>(NSApplicationActivationPolicyRegular));
  QVERIFY(child.state.value("finished").toBool());
  QCOMPARE(child.state.value("policy").toInt(-1), static_cast<int>(NSApplicationActivationPolicyProhibited));
  QVERIFY(child.errors.contains("became a foreground application"));
}

void MacCoreApplicationTests::actualCoreEarlyExit_data()
{
  QTest::addColumn<QStringList>("args");
  QTest::addColumn<int>("expectedExit");
  QTest::addColumn<bool>("duplicate");
  QTest::newRow("help") << QStringList{"--help"} << s_exitSuccess << false;
  QTest::newRow("version") << QStringList{"--version"} << s_exitSuccess << false;
  QTest::newRow("duplicate") << QStringList{"client"} << s_exitDuplicate << true;
}

void MacCoreApplicationTests::actualCoreEarlyExit()
{
  QFETCH(QStringList, args);
  QFETCH(int, expectedExit);
  QFETCH(bool, duplicate);
  QSharedMemory existingCore(kCoreBinName);
  if (duplicate)
    QVERIFY2(existingCore.create(1), "This isolated startup test requires no running Deskflow core");

  const auto child = runChild(QStringList{"--core-entry-child"} + args);
  QVERIFY2(child.finished, child.errors.constData());
  QCOMPARE(child.exitCode, 0);
  QCOMPARE(child.state.value("coreExit").toInt(-1), expectedExit);
  QCOMPARE(child.state.value("policy").toInt(-1), static_cast<int>(NSApplicationActivationPolicyProhibited));
  QVERIFY(!child.state.value("environmentSet").toBool());
}

int main(int argc, char **argv)
{
  @autoreleasepool {
    if (argc > 1 && std::strcmp(argv[1], "--core-entry-child") == 0) {
      const int result = deskflowCoreMain(argc - 1, argv + 1);
      qInstallMessageHandler(nullptr); // Production Log has been destroyed.
      return printState({{"coreExit", result}});
    }

    if (argc > 1 &&
        (std::strcmp(argv[1], "--bootstrap-child") == 0 || std::strcmp(argv[1], "--qt-control-child") == 0)) {
      deskflow::MacCoreApplicationBootstrap bootstrap;
      const bool prepared = std::strcmp(argv[1], "--bootstrap-child") == 0 && bootstrap.prepare();
      QApplication application(argc, argv);
      const int beforePolicy = static_cast<int>([NSApp activationPolicy]);
      const bool finished = bootstrap.finish();
      const bool clipboardAvailable = QApplication::clipboard() != nullptr;
      bool eventDelivered = false;
      QTimer::singleShot(0, &application, [&eventDelivered] { eventDelivered = true; });
      QCoreApplication::processEvents();
      return printState({
          {"prepared", prepared},
          {"beforePolicy", beforePolicy},
          {"finished", finished},
          {"clipboardAvailable", clipboardAvailable},
          {"eventDelivered", eventDelivered},
      });
    }

    QCoreApplication application(argc, argv);
    MacCoreApplicationTests tests;
    return QTest::qExec(&tests, argc, argv);
  }
}
