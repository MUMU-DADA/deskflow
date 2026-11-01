/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerTests.h"

#include "../deskflow/MockEventQueue.h"
#include "base/EventQueueTimer.h"
#include "deskflow/AppUtil.h"
#include "deskflow/IPrimaryComputer.h"
#include "deskflow/ipc/AlignmentEvent.h"
#include "deskflow/ipc/CoreIpc.h"
#include "deskflow/ipc/CoreIpcServer.h"
#include "io/IStream.h"
#include "server/ClientProxy1_0.h"
#include "server/PrimaryClient.h"
#include "server/Server.h"

#include <QJsonArray>
#include <map>
#include <memory>

namespace {

class GuardAppUtil : public AppUtil
{
public:
  int run() override
  {
    return 0;
  }
  std::vector<std::string> getKeyboardLayoutList() override
  {
    return {};
  }
  std::string getCurrentLanguageCode() override
  {
    return "en";
  }
};

class GuardEventQueue : public MockEventQueue
{
public:
  void addHandler(EventTypes type, void *target, const EventHandler &handler) override
  {
    handlers[{type, target}] = handler;
  }
  void removeHandler(EventTypes type, void *target) override
  {
    handlers.erase({type, target});
  }
  bool dispatchEvent(const Event &event) override
  {
    auto it = handlers.find({event.getType(), event.getTarget()});
    if (it == handlers.end())
      return false;
    const auto handler = it->second; // handlers may remove themselves
    handler(event);
    return true;
  }
  EventQueueTimer *newTimer(double, void *) override
  {
    timers.push_back(std::make_unique<EventQueueTimer>());
    return timers.back().get();
  }
  EventQueueTimer *newOneShotTimer(double seconds, void *target) override
  {
    return newTimer(seconds, target);
  }
  void addEvent(Event &&event) override
  {
    if (event.getType() == EventTypes::ServerConnected)
      delete static_cast<Server::ComputerConnectedInfo *>(event.getData());
    else
      Event::deleteData(event);
  }

private:
  std::map<std::pair<EventTypes, void *>, EventHandler> handlers;
  std::vector<std::unique_ptr<EventQueueTimer>> timers;
};

class GuardPrimary : public PrimaryClient
{
public:
  GuardPrimary() : PrimaryClient("guard-primary", nullptr)
  {
  }
  bool guarded = false;
  int enters = 0;
  int leaves = 0;
  int keyDowns = 0;
  uint32_t sides = 0;
  QRect shape{0, 0, 100, 100};
  void *getEventTarget() const override
  {
    return const_cast<GuardPrimary *>(this);
  }
  void getShape(int32_t &x, int32_t &y, int32_t &w, int32_t &h) const override
  {
    x = shape.x();
    y = shape.y();
    w = shape.width();
    h = shape.height();
  }
  void getCursorPos(int32_t &x, int32_t &y) const override
  {
    x = y = 50;
  }
  void getCursorCenter(int32_t &x, int32_t &y) const override
  {
    x = y = 50;
  }
  int32_t getJumpZoneSize() const override
  {
    return 1;
  }
  void reconfigure(uint32_t activeSides) override
  {
    sides = activeSides;
  }
  KeyModifierMask getToggleMask() const override
  {
    return 0;
  }
  uint32_t registerHotKey(KeyID, KeyModifierMask) override
  {
    return 1;
  }
  void unregisterHotKey(uint32_t) override
  {
  }
  void enable() override
  {
  }
  void disable() override
  {
  }
  bool isLockedToComputer() const override
  {
    return false;
  }
  bool isForegroundGameGuarded() const override
  {
    return guarded;
  }
  void enter(int32_t, int32_t, uint32_t, KeyModifierMask, bool) override
  {
    ++enters;
  }
  bool leave() override
  {
    ++leaves;
    return true;
  }
  void resetOptions() override
  {
  }
  void setOptions(const OptionsList &) override
  {
  }
  std::string getSecureInputApp() const override
  {
    return {};
  }
  void mouseMove(int32_t, int32_t) override
  {
  }
  void mouseDown(ButtonID) override
  {
  }
  void mouseUp(ButtonID) override
  {
  }
  void keyDown(KeyID, KeyModifierMask, KeyButton, const std::string &) override
  {
    ++keyDowns;
  }
};

class GuardStream : public deskflow::IStream
{
public:
  void close() override
  {
  }
  uint32_t read(void *, uint32_t) override
  {
    return 0;
  }
  void write(const void *, uint32_t) override
  {
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
    return const_cast<GuardStream *>(this);
  }
  bool isReady() const override
  {
    return false;
  }
  uint32_t getSize() const override
  {
    return 0;
  }
};

class GuardRemote : public ClientProxy1_0
{
public:
  explicit GuardRemote(IEventQueue *events, const std::string &name = "guard-remote")
      : ClientProxy1_0(name, new GuardStream, events)
  {
  }
  bool guarded = false;
  int enters = 0;
  int leaves = 0;
  QRect shape{0, 0, 100, 100};
  QPoint enteredAt;
  bool calibration = false;
  QList<QJsonObject> messages;
  bool alignmentCapable() const override
  {
    return calibration;
  }
  bool alignmentUiReady() const override
  {
    return calibration;
  }
  bool sendAlignmentMessage(const QJsonObject &message) override
  {
    if (!calibration)
      return false;
    messages.append(message);
    return true;
  }
  bool isForegroundGameGuarded() const override
  {
    return guarded;
  }
  void getShape(int32_t &x, int32_t &y, int32_t &w, int32_t &h) const override
  {
    x = shape.x();
    y = shape.y();
    w = shape.width();
    h = shape.height();
  }
  void getCursorPos(int32_t &x, int32_t &y) const override
  {
    x = y = 50;
  }
  void enter(int32_t x, int32_t y, uint32_t, KeyModifierMask, bool) override
  {
    ++enters;
    enteredAt = QPoint(x, y);
  }
  bool leave() override
  {
    ++leaves;
    return true;
  }
};

struct GuardServerFixture
{
  GuardEventQueue events;
  deskflow::server::Config config{&events};
  GuardPrimary primary;
  GuardRemote *remote = nullptr; // adopted and deleted by Server
  std::unique_ptr<Server> server;

  explicit GuardServerFixture(int delay = 0, bool alias = false)
  {
    static deskflow::core::ipc::CoreIpcServer ipc(nullptr); // no listening socket
    config.addComputer("guard-primary");
    config.addComputer("guard-remote");
    if (alias)
      config.addAlias("guard-remote", "remote-alias");
    config.connect("guard-primary", Direction::Right, 0, 1, "guard-remote", 0, 1);
    config.connect("guard-remote", Direction::Left, 0, 1, "guard-primary", 0, 1);
    config.addOption("", kOptionClipboardSharing, 0);
    config.addOption("", kOptionComputerSwitchDelay, delay);
    // All platform interactions go through the primary spy. A direct Computer
    // access would fail this test instead of silently touching the real desktop.
    server = std::make_unique<Server>(config, &primary, reinterpret_cast<deskflow::Computer *>(0x1), &events);
    remote = new GuardRemote(&events, alias ? "remote-alias" : "guard-remote");
    server->adoptClient(remote);
  }

  void motion(bool local, int x, int y)
  {
    IPrimaryComputer::MotionInfo info{x, y};
    events.dispatchEvent(Event(
        local ? EventTypes::PrimaryComputerMotionOnPrimary : EventTypes::PrimaryComputerMotionOnSecondary,
        primary.getEventTarget(), &info, Event::EventFlags::DontFreeData
    ));
  }

  void switchTo(const std::string &name)
  {
    Server::SwitchToComputerInfo info(name);
    events.dispatchEvent(
        Event(EventTypes::ServerSwitchToComputer, config.getInputFilter(), &info, Event::EventFlags::DontFreeData)
    );
  }

  void manualLock(bool on)
  {
    Server::LockCursorToComputerInfo info(
        on ? Server::LockCursorToComputerInfo::kOn : Server::LockCursorToComputerInfo::kOff
    );
    events.dispatchEvent(
        Event(EventTypes::ServerLockCursorToComputer, config.getInputFilter(), &info, Event::EventFlags::DontFreeData)
    );
  }

  void alignment(QJsonObject message, bool peer = false)
  {
    message.insert("session", "23456789-abcd-4567-89ab-0123456789ab");
    AlignmentEventData data(AlignmentEventData::Kind::Message, message, 1);
    events.dispatchEvent(Event(
        peer ? EventTypes::AlignmentPeer : EventTypes::AlignmentLocal,
        peer ? remote->getEventTarget() : alignmentEventTarget(), &data
    ));
  }
  void startAlignment(int revision = 1)
  {
    remote->calibration = true;
    AlignmentEventData ready(AlignmentEventData::Kind::UiAvailability, {}, 1, true);
    events.dispatchEvent(Event(EventTypes::AlignmentLocal, alignmentEventTarget(), &ready));
    for (const auto &name : {"guard-primary", "guard-remote"})
      alignment(
          {{"type", "show"},
           {"target", name},
           {"edge", "left"},
           {"points", QJsonArray{0.2, 0.8}},
           {"revision", revision}}
      );
  }
  void button(bool down)
  {
    IPrimaryComputer::ButtonInfo info(1, 0);
    events.dispatchEvent(Event(
        down ? EventTypes::PrimaryComputerButtonDown : EventTypes::PrimaryComputerButtonUp, config.getInputFilter(),
        &info, Event::EventFlags::DontFreeData
    ));
  }
  void escape()
  {
    static GuardAppUtil appUtil;
    IKeyState::KeyInfo info{kKeyEscape, 0, 1, 1, ""};
    events.dispatchEvent(
        Event(EventTypes::KeyStateKeyDown, config.getInputFilter(), &info, Event::EventFlags::DontFreeData)
    );
  }
};

} // namespace

void ServerTests::SwitchToComputerInfo_alloc_computer()
{
  auto actual = new Server::SwitchToComputerInfo("test");
  QCOMPARE(actual->m_computer, "test");
  delete actual;
}

void ServerTests::KeyboardBroadcastInfo_alloc_stateAndComputers()
{
  auto info = new Server::KeyboardBroadcastInfo(Server::KeyboardBroadcastInfo::State::kOn, "test");
  QCOMPARE(info->m_state, Server::KeyboardBroadcastInfo::State::kOn);
  QCOMPARE(info->m_computers, "test");
  delete info;
}

void ServerTests::fullscreenGameGuard_activeDeviceOnlyAndExplicitSwitch()
{
  GuardServerFixture test;
  test.remote->guarded = true;
  test.motion(true, 100, 50);
  QCOMPARE(test.remote->enters, 1); // background remote game did not lock primary
  test.primary.guarded = true;
  test.motion(false, -200, 0);
  QCOMPARE(test.remote->leaves, 0); // active remote game prevents edge crossing
  test.switchTo("guard-primary");
  QCOMPARE(test.remote->leaves, 1); // explicit hotkey bypasses automatic guard
  QCOMPARE(test.primary.enters, 1);
  test.motion(true, 100, 50);
  QCOMPARE(test.remote->enters, 1);          // active local game also protects
  QCOMPARE(test.primary.sides, uint32_t(0)); // low-level jump zones disabled
  test.primary.guarded = false;
  test.motion(true, 100, 50);
  QCOMPARE(test.remote->enters, 2); // exit fullscreen restores crossing
  test.primary.guarded = true;
  test.remote->guarded = false;
  test.motion(false, -200, 0);
  QCOMPARE(test.remote->leaves, 2); // background primary game cannot lock remote
  QCOMPARE(test.primary.enters, 2);
}

void ServerTests::fullscreenGameGuard_cancelsDelayedSwitch()
{
  GuardServerFixture test(250);
  test.motion(true, 100, 50);
  QCOMPARE(test.remote->enters, 0);
  test.primary.guarded = true;
  QVERIFY(test.events.dispatchEvent(Event(EventTypes::Timer, test.server.get())));
  QCOMPARE(test.remote->enters, 0); // game became foreground while delay was pending
  test.primary.guarded = false;
  test.motion(true, 100, 50);
  QVERIFY(test.events.dispatchEvent(Event(EventTypes::Timer, test.server.get())));
  QCOMPARE(test.remote->enters, 1);
}

void ServerTests::fullscreenGameGuard_preservesManualLock()
{
  GuardServerFixture test;
  test.manualLock(true);
  test.primary.guarded = true;
  test.motion(true, 100, 50);
  test.primary.guarded = false;
  test.motion(true, 100, 50);
  QCOMPARE(test.remote->enters, 0); // auto release must not clear the manual lock
  test.manualLock(false);
  test.motion(true, 100, 50);
  QCOMPARE(test.remote->enters, 1);
}

void ServerTests::calibrationDragBlocksOnlyActiveEdgesAndEscapeReleases()
{
  GuardServerFixture test;
  test.startAlignment();
  test.button(true); // Input thread protects before the GUI's asynchronous drag message.
  test.motion(true, 100, 50);
  QCOMPARE(test.remote->enters, 0);
  test.switchTo("guard-remote"); // Explicit switching remains available.
  QCOMPARE(test.remote->enters, 1);
  test.motion(false, -200, 0); // The primary's button hold must not lock the other device.
  QCOMPARE(test.primary.enters, 1);
  test.escape();
  QCOMPARE(test.primary.keyDowns, 1); // Escape is still delivered normally.
  QCOMPARE(test.remote->messages.last().value("reason").toString(), QString("escape"));
  test.motion(true, 100, 50);
  QCOMPARE(test.remote->enters, 2);
}

void ServerTests::calibrationMissingMouseUpExpiresDespitePings()
{
  GuardServerFixture test;
  test.startAlignment();
  test.button(true);
  test.motion(true, 100, 50);
  QCOMPARE(test.remote->enters, 0);
  for (int i = 0; i < 3; ++i) {
    QTest::qWait(1050);
    test.alignment({{"type", "ping"}, {"target", "guard-remote"}});
  }
  test.motion(true, 100, 50);
  QCOMPARE(test.remote->enters, 1); // Session alive; missing button-up cannot lock indefinitely.
}

void ServerTests::calibrationRevisionAliasesAndManualLock()
{
  GuardServerFixture test(0, true);
  test.startAlignment();
  QCOMPARE(test.remote->messages.last().value("target").toString(), QString("remote-alias"));
  QCOMPARE(test.remote->messages.last().value("source").toString(), QString("guard-primary"));
  test.switchTo("guard-remote");
  auto point =
      QJsonObject{{"type", "points"}, {"target", "guard-primary"}, {"points", QJsonArray{0.2, 0.8}}, {"revision", 1},
                  {"seq", 1},         {"dragging", true}};
  test.alignment(point, true);
  test.motion(false, -200, 0);
  QCOMPARE(test.primary.enters, 0);
  test.startAlignment(2);
  point["seq"] = 100; // A delayed old-revision packet must not reinstate its drag hold.
  test.alignment(point, true);
  test.motion(false, -200, 0);
  QCOMPARE(test.primary.enters, 1);
  test.manualLock(true);
  test.alignment({{"type", "close"}, {"target", "guard-remote"}});
  QCOMPARE(test.remote->messages.last().value("target").toString(), QString("remote-alias"));
  test.motion(true, 100, 50);
  QCOMPARE(test.remote->enters, 1);
  test.manualLock(false);
  test.motion(true, 100, 50);
  QCOMPARE(test.remote->enters, 2);
}

void ServerTests::edgeMappingStaysInsideDestination_data()
{
  QTest::addColumn<int>("direction");
  QTest::addColumn<int>("sourcePixel");
  QTest::addColumn<float>("sourceStart");
  QTest::addColumn<float>("sourceEnd");
  QTest::addColumn<float>("destinationStart");
  QTest::addColumn<float>("destinationEnd");
  QTest::addColumn<int>("expectedPixel");
  for (const auto direction : {Direction::Left, Direction::Right, Direction::Top, Direction::Bottom}) {
    const auto name = QByteArray(deskflow::server::Config::dirName(direction));
    QTest::newRow((name + "-narrow-upper-edge").constData())
        << int(direction) << 32766 << 0.0f << 1.0f << 0.999f << 1.0f << 1079;
    QTest::newRow((name + "-default-last-pixel").constData())
        << int(direction) << 32766 << 0.0f << 1.0f << 0.0f << 1.0f << 1079;
    QTest::newRow((name + "-default-midpoint").constData())
        << int(direction) << 16383 << 0.0f << 1.0f << 0.0f << 1.0f << 540;
    QTest::newRow((name + "-partial-edge").constData())
        << int(direction) << 16383 << 0.25f << 0.75f << 0.25f << 0.75f << 540;
    QTest::newRow((name + "-narrow-lower-edge").constData())
        << int(direction) << 0 << 0.0f << 1.0f << 0.0f << 0.001f << 0;
  }
}

void ServerTests::edgeMappingStaysInsideDestination()
{
  QFETCH(int, direction);
  QFETCH(int, sourcePixel);
  QFETCH(float, sourceStart);
  QFETCH(float, sourceEnd);
  QFETCH(float, destinationStart);
  QFETCH(float, destinationEnd);
  QFETCH(int, expectedPixel);
  const auto side = static_cast<Direction>(direction);
  const bool horizontal = side == Direction::Left || side == Direction::Right;
  GuardServerFixture test;
  test.primary.shape = QRect(0, 0, 32767, 32767);
  test.remote->shape = QRect(-1920, -1080, 1080, 1080);
  QVERIFY(test.config.disconnect("guard-primary", Direction::Right));
  QVERIFY(test.config.connect(
      "guard-primary", side, sourceStart, sourceEnd, "guard-remote", destinationStart, destinationEnd
  ));

  // Dispatch actual primary motion so Config edge transforms and Server's
  // private fraction/pixel mapping run together, including the crossing axis.
  const int crossingPixel = (side == Direction::Left || side == Direction::Top) ? 0 : 32766;
  test.motion(true, horizontal ? crossingPixel : sourcePixel, horizontal ? sourcePixel : crossingPixel);
  QCOMPARE(test.remote->enters, 1);
  QVERIFY(test.remote->shape.contains(test.remote->enteredAt));
  if (horizontal)
    QCOMPARE(test.remote->enteredAt.y(), test.remote->shape.y() + expectedPixel);
  else
    QCOMPARE(test.remote->enteredAt.x(), test.remote->shape.x() + expectedPixel);
}

QTEST_MAIN(ServerTests)
