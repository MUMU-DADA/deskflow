/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerTests.h"

#include "../deskflow/MockEventQueue.h"
#include "base/EventQueueTimer.h"
#include "deskflow/IPrimaryComputer.h"
#include "deskflow/ipc/CoreIpcServer.h"
#include "io/IStream.h"
#include "server/ClientProxy1_0.h"
#include "server/PrimaryClient.h"
#include "server/Server.h"

#include <map>
#include <memory>

namespace {

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
  uint32_t sides = 0;
  void *getEventTarget() const override
  {
    return const_cast<GuardPrimary *>(this);
  }
  void getShape(int32_t &x, int32_t &y, int32_t &w, int32_t &h) const override
  {
    x = y = 0;
    w = h = 100;
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
  explicit GuardRemote(IEventQueue *events) : ClientProxy1_0("guard-remote", new GuardStream, events)
  {
  }
  bool guarded = false;
  int enters = 0;
  int leaves = 0;
  bool isForegroundGameGuarded() const override
  {
    return guarded;
  }
  void getShape(int32_t &x, int32_t &y, int32_t &w, int32_t &h) const override
  {
    x = y = 0;
    w = h = 100;
  }
  void getCursorPos(int32_t &x, int32_t &y) const override
  {
    x = y = 50;
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
};

struct GuardServerFixture
{
  GuardEventQueue events;
  deskflow::server::Config config{&events};
  GuardPrimary primary;
  GuardRemote *remote = nullptr; // adopted and deleted by Server
  std::unique_ptr<Server> server;

  explicit GuardServerFixture(int delay = 0)
  {
    static deskflow::core::ipc::CoreIpcServer ipc(nullptr); // no listening socket
    config.addComputer("guard-primary");
    config.addComputer("guard-remote");
    config.connect("guard-primary", Direction::Right, 0, 1, "guard-remote", 0, 1);
    config.connect("guard-remote", Direction::Left, 0, 1, "guard-primary", 0, 1);
    config.addOption("", kOptionClipboardSharing, 0);
    config.addOption("", kOptionComputerSwitchDelay, delay);
    // All platform interactions go through the primary spy. A direct Computer
    // access would fail this test instead of silently touching the real desktop.
    server = std::make_unique<Server>(config, &primary, reinterpret_cast<deskflow::Computer *>(0x1), &events);
    remote = new GuardRemote(&events);
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

QTEST_MAIN(ServerTests)
