/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClientProxyTests.h"

#include "../deskflow/MockEventQueue.h"
#include "deskflow/AppUtil.h"
#include "deskflow/DeskflowException.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"
#include "server/ClientProxy1_0.h"
#include "server/ClientProxy1_1.h"
#include "server/ClientProxy1_6.h"
#include "server/ClientProxy1_7.h"
#include "server/ClientProxy1_8.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <QByteArray>
#include <QTest>

namespace {

class TestAppUtil : public AppUtil
{
public:
  int run() override
  {
    return 0;
  }

  std::vector<std::string> getKeyboardLayoutList() override
  {
    return {"en"};
  }

  std::string getCurrentLanguageCode() override
  {
    return "en";
  }
};

class CapturingStream : public deskflow::IStream
{
public:
  QByteArray take()
  {
    auto bytes = m_buffer;
    m_buffer.clear();
    return bytes;
  }

  void write(const void *buffer, uint32_t n) override
  {
    m_buffer.append(static_cast<const char *>(buffer), n);
  }

  void close() override
  {
  }

  void feed(const QByteArray &bytes)
  {
    m_input.append(bytes);
  }

  uint32_t read(void *buffer, uint32_t size) override
  {
    const auto count = std::min<uint32_t>(size, static_cast<uint32_t>(m_input.size()));
    if (buffer && count) {
      std::memcpy(buffer, m_input.constData(), count);
    }
    m_input.remove(0, count);
    return count;
  }

  void flush() override
  {
  }

  void shutdownInput() override
  {
  }

  void shutdownOutput() override
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<CapturingStream *>(this);
  }

  bool isReady() const override
  {
    return false;
  }

  uint32_t getSize() const override
  {
    return 0;
  }

private:
  QByteArray m_buffer;
  QByteArray m_input;
};

class GameGuardProxy : public ClientProxy1_0
{
public:
  using ClientProxy1_0::ClientProxy1_0;

  bool activity(CapturingStream *stream, uint8_t kind)
  {
    CapturingStream packet;
    ProtocolUtil::writef(&packet, kMsgDActivity, kind);
    const auto bytes = packet.take();
    stream->feed(bytes.mid(4));
    return parseMessage(reinterpret_cast<const uint8_t *>(bytes.constData()));
  }

  bool report(CapturingStream *stream, uint32_t sequence, uint8_t guarded)
  {
    CapturingStream packet;
    ProtocolUtil::writef(&packet, kMsgDFullscreenGameGuard, sequence, guarded);
    const auto bytes = packet.take();
    stream->feed(bytes.mid(4));
    return parseMessage(reinterpret_cast<const uint8_t *>(bytes.constData()));
  }
};

std::unique_ptr<ClientProxy> makeProxy(int minor, deskflow::IStream *stream, IEventQueue *events)
{
  // the 1.4 and later constructors assert the server pointer is non-null but only store it
  auto *server = reinterpret_cast<Server *>(0x1);

  std::unique_ptr<ClientProxy> proxy;
  switch (minor) {
  case 0:
    proxy = std::make_unique<ClientProxy1_0>("client", stream, events);
    break;
  case 1:
    proxy = std::make_unique<ClientProxy1_1>("client", stream, events);
    break;
  case 6:
    proxy = std::make_unique<ClientProxy1_6>("client", stream, server, events);
    break;
  case 7:
    proxy = std::make_unique<ClientProxy1_7>("client", stream, server, events);
    break;
  case 8:
    proxy = std::make_unique<ClientProxy1_8>("client", stream, server, events);
    break;
  default:
    break;
  }
  return proxy;
}

struct ProxyUnderTest
{
  MockEventQueue events;
  CapturingStream *stream = new CapturingStream;
  std::unique_ptr<ClientProxy> proxy;

  explicit ProxyUnderTest(int minor) : proxy(makeProxy(minor, stream, &events))
  {
    // drop the query-info and layout-sync messages the constructors send
    stream->take();
  }
};

const KeyID kKey = 0x61;
const KeyModifierMask kMask = 0;
const KeyButton kButton = 0x1e;
const int32_t kCount = 3;
const std::string kLang = "en";

} // namespace

void ClientProxyTests::initTestCase()
{
  // the 1.8 constructor reads the keyboard layouts through AppUtil::instance()
  static TestAppUtil appUtil;
}

// These formats are frozen because shipped third-party clients parse them byte
// for byte: Synergy 1.4 through 1.14.1 negotiate 1.4 through 1.7, Barrier and
// Input Leap negotiate 1.6, and Synergy 1.14.2 onwards and Deskflow negotiate 1.8.
void ClientProxyTests::keyDown_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKDN" + QByteArray::fromHex("0061 0000");
  QTest::newRow("1.1") << 1 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.6") << 6 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.7") << 7 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.8") << 8 << "DKDL" + QByteArray::fromHex("0061 0000 001e 00000002") + "en";
}

void ClientProxyTests::keyDown()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyDown(kKey, kMask, kButton, kLang);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::keyRepeat_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKRP" + QByteArray::fromHex("0061 0000 0003");
  QTest::newRow("1.1") << 1 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.6") << 6 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.7") << 7 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.8") << 8 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e 00000002") + "en";
}

void ClientProxyTests::keyRepeat()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyRepeat(kKey, kMask, kCount, kButton, kLang);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::keyUp_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKUP" + QByteArray::fromHex("0061 0000");
  QTest::newRow("1.1") << 1 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.6") << 6 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.7") << 7 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.8") << 8 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
}

void ClientProxyTests::keyUp()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyUp(kKey, kMask, kButton);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::fullscreenGameGuardRequiresCapability()
{
  MockEventQueue events;
  auto *stream = new CapturingStream;
  GameGuardProxy proxy("client", stream, &events);
  proxy.enter(10, 20, 1, 0, false);
  // A legacy client is not held waiting for a packet it cannot send.
  QVERIFY(!proxy.isForegroundGameGuarded());
  QVERIFY_EXCEPTION_THROWN(proxy.report(stream, 1, 1), BadClientException);
}

void ClientProxyTests::fullscreenGameGuardTracksActiveVisit()
{
  MockEventQueue events;
  auto *stream = new CapturingStream;
  GameGuardProxy proxy("client", stream, &events);
  proxy.setOptions({kOptionFullscreenGameGuard, 1});
  QVERIFY(proxy.report(stream, 0, 0)); // capability acknowledgement while inactive
  QVERIFY(!proxy.isForegroundGameGuarded());

  proxy.enter(10, 20, 1, 0, false);
  QVERIFY(proxy.isForegroundGameGuarded()); // wait for a fresh observation
  QVERIFY(proxy.report(stream, 0, 0));
  QVERIFY(proxy.isForegroundGameGuarded()); // stale unlock must not release this visit
  QVERIFY(proxy.report(stream, 1, 1));
  QVERIFY(proxy.isForegroundGameGuarded());
  QVERIFY(proxy.report(stream, 1, 0));
  QVERIFY(!proxy.isForegroundGameGuarded()); // focus/fullscreen exited
  QVERIFY(proxy.report(stream, 1, 1));
  QVERIFY(proxy.isForegroundGameGuarded());

  // Explicit switching uses leave() directly and remains available while guarded.
  QVERIFY(proxy.leave());
  QVERIFY(!proxy.isForegroundGameGuarded());
  QVERIFY(proxy.report(stream, 1, 1));
  QVERIFY(!proxy.isForegroundGameGuarded()); // inactive device cannot hold focus

  proxy.enter(30, 40, 2, 0, false);
  QVERIFY(proxy.isForegroundGameGuarded());
  QVERIFY(proxy.report(stream, 1, 0));
  QVERIFY(proxy.isForegroundGameGuarded());
  QVERIFY(proxy.report(stream, 2, 0));
  QVERIFY(!proxy.isForegroundGameGuarded());

  proxy.resetOptions();
  proxy.setOptions({kOptionFullscreenGameGuard, 1});
  QVERIFY(proxy.report(stream, 2, 1));
  QVERIFY(proxy.isForegroundGameGuarded()); // config reload preserves active sequence
}

void ClientProxyTests::fullscreenGameGuardRejectsInvalidPayload()
{
  MockEventQueue events;
  auto *stream = new CapturingStream;
  GameGuardProxy proxy("client", stream, &events);
  proxy.setOptions({kOptionFullscreenGameGuard, 1});
  QVERIFY_EXCEPTION_THROWN(proxy.report(stream, 1, 2), BadClientException);
}

void ClientProxyTests::fullscreenGameGuardResetsOnNewConnection()
{
  MockEventQueue events;
  {
    auto *stream = new CapturingStream;
    GameGuardProxy proxy("client", stream, &events);
    proxy.setOptions({kOptionFullscreenGameGuard, 1});
    proxy.enter(10, 20, 1, 0, false);
    QVERIFY(proxy.report(stream, 1, 1));
    QVERIFY(proxy.isForegroundGameGuarded());
  }
  auto *stream = new CapturingStream;
  GameGuardProxy reconnected("client", stream, &events);
  reconnected.enter(10, 20, 2, 0, false);
  QVERIFY(!reconnected.isForegroundGameGuarded());
}

void ClientProxyTests::activityRequiresOptInAndResets()
{
  MockEventQueue events;
  auto *stream = new CapturingStream;
  GameGuardProxy proxy("client", stream, &events);
  stream->take();
  proxy.sendActivity();
  QVERIFY(stream->take().isEmpty()); // legacy client, no negotiated extension
  proxy.setOptions({kOptionActivitySync, 1});
  stream->take();
  proxy.sendActivity();
  QVERIFY(stream->take().isEmpty()); // capability alone does not opt in
  QVERIFY(proxy.activity(stream, 1));
  QVERIFY(proxy.activitySyncEnabled());
  proxy.sendActivity();
  QCOMPARE(stream->take(), QByteArray("DACT") + QByteArray(1, char(2)));
  QVERIFY(proxy.activity(stream, 0));
  QVERIFY(!proxy.activitySyncEnabled());
  proxy.sendActivity();
  QVERIFY(stream->take().isEmpty());
  QVERIFY(proxy.activity(stream, 1));
  proxy.resetOptions();
  proxy.setOptions({kOptionActivitySync, 1});
  QVERIFY(!proxy.activitySyncEnabled()); // configuration reset needs a new opt-in
  auto *freshStream = new CapturingStream;
  GameGuardProxy fresh("client", freshStream, &events);
  QVERIFY(!fresh.activitySyncEnabled()); // never retained by computer name
}

void ClientProxyTests::activityRejectsInvalidPayload()
{
  for (const uint8_t kind : {0, 1, 2, 3, 255}) {
    MockEventQueue events;
    auto *stream = new CapturingStream;
    GameGuardProxy proxy("client", stream, &events);
    QVERIFY(!proxy.activity(stream, kind)); // unadvertised extension
  }
  for (const uint8_t kind : {2, 3, 255}) {
    MockEventQueue events;
    auto *stream = new CapturingStream;
    GameGuardProxy proxy("client", stream, &events);
    proxy.setOptions({kOptionActivitySync, 1});
    QVERIFY(!proxy.activity(stream, kind)); // input before opt-in or invalid kind
  }
}

void ClientProxyTests::legacyClientActivityIsIgnored()
{
  class Events : public MockEventQueue
  {
  public:
    int reports = 0;
    void addEvent(Event &&event) override
    {
      ++reports;
      Event::deleteData(event);
    }
  } events;
  auto *stream = new CapturingStream;
  GameGuardProxy proxy("client", stream, &events);
  proxy.setOptions({kOptionActivitySync, 1});
  QVERIFY(proxy.activity(stream, 1));
  QCOMPARE(events.reports, 0);
  QVERIFY(proxy.activity(stream, 2));
  QCOMPARE(events.reports, 0); // legacy local input never reaches the server
  proxy.enter(10, 20, 1, 0, false);
  for (int i = 0; i < 100; ++i)
    QVERIFY(proxy.activity(stream, 2));
  QCOMPARE(events.reports, 0);          // also ignored while the shared cursor is here
  QVERIFY(proxy.activitySyncEnabled()); // still accepts server activity
}

QTEST_MAIN(ClientProxyTests)
