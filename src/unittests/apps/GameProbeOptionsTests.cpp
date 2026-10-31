/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ProbeOptions.h"
#include "ProbeWait.h"

#include <QTest>
#include <QTimer>

class GameProbeOptionsTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void waitsProcessMainEvents()
  {
    QObject context;
    bool observed = false;
    QTimer::singleShot(0, &context, [&observed] { observed = true; });
    QVERIFY(deskflow::probe::waitUntil(std::chrono::steady_clock::now() + std::chrono::seconds(1), [&observed] {
      return observed;
    }));
  }

  void expiredWaitDoesNotBlock()
  {
    QVERIFY(!deskflow::probe::waitUntil(std::chrono::steady_clock::now(), [] { return false; }));
  }

  void boundedSampling_data()
  {
    QTest::addColumn<QString>("samplesText");
    QTest::addColumn<QString>("intervalText");
    QTest::addColumn<bool>("valid");
    QTest::newRow("default") << QString("1") << QString("500") << true;
    QTest::newRow("minimum-interval") << QString("2") << QString("100") << true;
    QTest::newRow("maximum-count") << QString("1200") << QString("100") << true;
    QTest::newRow("last-start-reserves-query") << QString("900") << QString("1000") << true;
    QTest::newRow("last-start-at-deadline") << QString("901") << QString("1000") << false;
    QTest::newRow("zero-count") << QString("0") << QString("500") << false;
    QTest::newRow("negative-count") << QString("-1") << QString("500") << false;
    QTest::newRow("excessive-count") << QString("1201") << QString("100") << false;
    QTest::newRow("count-overflow") << QString("9999999999999999") << QString("500") << false;
    QTest::newRow("fractional-count") << QString("1.5") << QString("500") << false;
    QTest::newRow("empty-count") << QString() << QString("500") << false;
    QTest::newRow("fast-interval") << QString("1") << QString("99") << false;
    QTest::newRow("slow-interval") << QString("1") << QString("60001") << false;
    QTest::newRow("maximum-interval") << QString("1") << QString("60000") << true;
    QTest::newRow("excessive-schedule") << QString("100") << QString("10000") << false;
    QTest::newRow("minute-interval") << QString("15") << QString("60000") << true;
  }

  void boundedSampling()
  {
    QFETCH(QString, samplesText);
    QFETCH(QString, intervalText);
    QFETCH(bool, valid);
    int samples = 0;
    int interval = 0;
    const auto error = deskflow::probe::validateSampling(samplesText, intervalText, samples, interval);
    QCOMPARE(error.isEmpty(), valid);
    if (valid) {
      QVERIFY(samples > 0);
      QVERIFY(interval >= 100);
      QVERIFY(
          static_cast<qint64>(samples - 1) * interval + deskflow::probe::kQueryTimeoutMs <=
          deskflow::probe::kMaximumDurationMs
      );
    }
  }

  void localX11Only_data()
  {
    QTest::addColumn<QString>("display");
    QTest::addColumn<bool>("allowed");
    QTest::newRow("unset") << QString() << true;
    QTest::newRow("local") << QString(":0") << true;
    QTest::newRow("local-screen") << QString(":0.1") << true;
    QTest::newRow("unix-host") << QString("unix:2") << true;
    QTest::newRow("unix-transport") << QString("unix/:2.0") << true;
    QTest::newRow("ssh-forward") << QString("localhost:10.0") << false;
    QTest::newRow("remote-host") << QString("example.org:0") << false;
    QTest::newRow("tcp-transport") << QString("tcp/localhost:0") << false;
    QTest::newRow("ipv6") << QString("[::1]:0") << false;
    QTest::newRow("malformed") << QString("unix/not-local:0") << false;
    QTest::newRow("trailing-newline") << QString(":0\n") << false;
  }

  void localX11Only()
  {
    QFETCH(QString, display);
    QFETCH(bool, allowed);
    QCOMPARE(deskflow::probe::isLocalX11Display(display), allowed);
  }
};

QTEST_GUILESS_MAIN(GameProbeOptionsTests)
#include "GameProbeOptionsTests.moc"
