/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "platform/ActivitySync.h"

#include <QTest>

using namespace std::chrono_literals;

namespace {
class FakeActivity : public deskflow::ActivitySync
{
public:
  std::optional<uint64_t> token = 123;
  int refreshes = 0;
  int releases = 0;

protected:
  std::optional<uint64_t> inputToken() const override
  {
    return token;
  }
  void refreshDisplay() override
  {
    ++refreshes;
  }
  void releaseDisplay() override
  {
    ++releases;
  }
};
} // namespace

class ActivitySyncTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void idleNeverRefreshes()
  {
    FakeActivity sync;
    const auto start = deskflow::ActivitySync::Clock::now();
    for (int i = 0; i < 1200; ++i)
      QVERIFY(!sync.poll(start + i * 1s));
    QCOMPARE(sync.refreshes, 0);
  }

  void notificationsCannotEcho()
  {
    FakeActivity left, right;
    const auto start = deskflow::ActivitySync::Clock::now();
    QVERIFY(!left.poll(start));
    QVERIFY(!right.poll(start));
    ++*left.token;
    QVERIFY(left.poll(start + 1s));
    right.receive(start + 1s);
    QCOMPARE(right.refreshes, 1);
    for (int i = 1; i < 1200; ++i) {
      QVERIFY(!left.poll(start + (i + 1) * 1s));
      QVERIFY(!right.poll(start + i * 1s));
    }
    QCOMPARE(left.refreshes, 0);
    QCOMPARE(right.refreshes, 1);
    ++*right.token;
    QVERIFY(right.poll(start + 1200s));
    left.receive(start + 1200s);
    QCOMPARE(left.refreshes, 1);
  }

  void inputIsCoalescedButNotRenewed()
  {
    FakeActivity sync;
    const auto start = deskflow::ActivitySync::Clock::now();
    QVERIFY(!sync.poll(start));
    for (int i = 1; i < 10; ++i) {
      sync.noteInput();
      QVERIFY(!sync.poll(start + i * 100ms));
    }
    QVERIFY(sync.poll(start + 1s));
    QVERIFY(!sync.poll(start + 2s));
    sync.receive(start);
    sync.receive(start + 100ms);
    QCOMPARE(sync.refreshes, 1);
    sync.receive(start + 1s);
    QCOMPARE(sync.refreshes, 2);
  }

  void timestampWrapAndInputFailures()
  {
    FakeActivity sync;
    const auto start = deskflow::ActivitySync::Clock::now();
    sync.token = UINT32_MAX;
    QVERIFY(!sync.poll(start));
    sync.token = 1;
    QVERIFY(sync.poll(start + 1s));
    sync.token = 0; // GetLastInputInfo timestamps can move backwards.
    QVERIFY(sync.poll(start + 2s));
    sync.token.reset();
    QVERIFY(!sync.poll(start + 3s));
    sync.token = 100;
    QVERIFY(!sync.poll(start + 4s)); // recovery establishes a baseline
  }

  void disconnectAndResumeDiscardOldInput()
  {
    FakeActivity sync;
    const auto start = deskflow::ActivitySync::Clock::now();
    QVERIFY(!sync.poll(start));
    sync.receive(start);
    sync.noteInput();
    sync.reset();
    QCOMPARE(sync.releases, 1);
    ++*sync.token;
    QVERIFY(!sync.poll(start + 1s));
    sync.noteInput();
    ++*sync.token;
    QVERIFY(!sync.poll(start + 30s)); // stale observations after suspension
    ++*sync.token;
    QVERIFY(sync.poll(start + 31s));
  }
};

QTEST_GUILESS_MAIN(ActivitySyncTests)
#include "ActivitySyncTests.moc"
