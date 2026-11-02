/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "FakeVolumeControl.h"
#include <QTest>

using namespace deskflow::volume;
using namespace std::chrono_literals;

class VolumeSyncTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void ratiosAndRecipientOnlyClamping()
  {
    QCOMPARE(scale({5000, false}, 100, 50).volume, uint16_t(2500));
    State source{8000, true};
    const auto recipient = scale(source, 50, 100);
    QCOMPARE(recipient.volume, uint16_t(10000));
    QCOMPARE(source.volume, uint16_t(8000));
    QVERIFY(recipient.muted);
    QCOMPARE(scale(source, 50, 25).volume, uint16_t(4000));
    QCOMPARE(scale({0, false}, 100, 1).volume, uint16_t(0));
    QCOMPARE(scale({10000, false}, 1, 100).volume, uint16_t(10000));
  }

  void referencesRejectInvalidConfiguration()
  {
    const auto weights = parseWeights(R"({"A":100,"B":50})");
    QVERIFY(weights);
    QCOMPARE(reference(*weights, "B"), 50);
    QCOMPARE(reference(*weights, "new-device"), 100);
    QCOMPARE(parseWeights(serializeWeights(*weights))->value("A"), 100);
    for (const auto &value :
         {"[]", "{\"A\":0}", "{\"A\":101}", "{\"A\":50.5}", "{\"A\":true}", "{\"A\":\"50\"}", "{\"\":50}"})
      QVERIFY(!parseWeights(value));
    QVERIFY(!parseWeights(QString(65537, ' ')));
  }

  void clientWaitsForServerAndDoesNotEchoReadback()
  {
    auto control = std::make_unique<FakeVolumeControl>();
    auto *audio = control.get();
    audio->current.state = {9000, true};
    ClientSession client(std::move(control));
    const auto start = Clock::now();
    QVERIFY(!client.poll(start));
    QCOMPARE(audio->reads, 0);
    client.enable(true);
    const auto ready = client.poll(start);
    QVERIFY(ready && ready->kind == Kind::Ready);
    QCOMPARE(audio->writes, 0);
    audio->current.state.volume = 8000;
    QVERIFY(!client.poll(start + 100ms)); // initial state is always server-authoritative
    PeerSession server;
    server.reset(true);
    QVERIFY(server.receive(*ready));
    const auto initial = server.set({2500, false}, Volume | Mute);
    QVERIFY(initial);
    QCOMPARE(client.receive(*initial, start + 150ms), ReceiveResult::Applied);
    QCOMPARE(audio->current.state.volume, uint16_t(2500));
    QVERIFY(!audio->current.state.muted);
    QVERIFY(!client.poll(start + 250ms));
    audio->current.state.volume = 8000;
    const auto changed = client.poll(start + 350ms);
    QVERIFY(changed && changed->kind == Kind::Change);
    QCOMPARE(changed->mask, Volume);
    QCOMPARE(changed->state.volume, uint16_t(8000));
    QVERIFY(!client.poll(start + 450ms));
  }

  void muteOnlyUpdatePreservesUnpolledLocalVolume()
  {
    auto control = std::make_unique<FakeVolumeControl>();
    auto *audio = control.get();
    Monitor monitor(std::move(control));
    const auto start = Clock::now();
    QVERIFY(monitor.poll(start));
    audio->current.state.volume = 8000;
    QVERIFY(monitor.apply({2500, true}, Mute));
    QCOMPARE(audio->current.state.volume, uint16_t(8000));
    QVERIFY(monitor.poll(start + 100ms));
    QCOMPARE(monitor.changes(), Volume);
    QVERIFY(monitor.snapshot()->state.muted);
  }

  void roundedAndClampedWritesDoNotBecomeLocalInput()
  {
    auto control = std::make_unique<FakeVolumeControl>();
    auto *audio = control.get();
    audio->roundStep = 63;
    Monitor monitor(std::move(control));
    const auto start = Clock::now();
    monitor.poll(start);
    QVERIFY(monitor.apply({2500, false}, Volume));
    QCOMPARE(audio->current.state.volume, uint16_t(2520));
    monitor.poll(start + 100ms);
    QCOMPARE(monitor.changes(), uint8_t(0));
    QVERIFY(monitor.apply(scale({8000, false}, 50, 100), Volume));
    monitor.poll(start + 200ms);
    QCOMPARE(monitor.changes(), uint8_t(0));
    audio->current.state.volume = 9999;
    monitor.poll(start + 300ms);
    QCOMPARE(monitor.changes(), Volume);
  }

  void deviceSwitchRejectsOldCommandsAndReinitializes()
  {
    auto control = std::make_unique<FakeVolumeControl>();
    auto *audio = control.get();
    ClientSession client(std::move(control));
    client.enable(true);
    const auto start = Clock::now();
    const auto first = client.poll(start);
    QVERIFY(first);
    Message initial{Kind::Set, first->epoch, 1, {2500, false}, Volume | Mute};
    QCOMPARE(client.receive(initial, start), ReceiveResult::Applied);
    audio->current.deviceId = "different-output";
    initial.sequence = 2;
    QCOMPARE(client.receive(initial, start + 50ms), ReceiveResult::Failed);
    QCOMPARE(audio->writes, 1); // never wrote the old command to the new endpoint
    QVERIFY(!client.poll(start + 500ms));
    const auto second = client.poll(start + 1100ms);
    QVERIFY(second && second->kind == Kind::Ready && second->epoch != first->epoch);
    QCOMPARE(client.receive(initial, start + 1200ms), ReceiveResult::Ignored);
    initial.epoch = second->epoch;
    initial.sequence = 1;
    initial.state.volume = 1000;
    QCOMPARE(client.receive(initial, start + 1200ms), ReceiveResult::Applied);
    QVERIFY(!client.poll(start + 1300ms));
    QCOMPARE(audio->current.state.volume, uint16_t(1000));
  }

  void unavailableAndDisconnectDoNotWriteOrReplay()
  {
    auto control = std::make_unique<FakeVolumeControl>();
    auto *audio = control.get();
    audio->present = false;
    ClientSession client(std::move(control));
    const auto start = Clock::now();
    client.enable(true);
    const auto absent = client.poll(start);
    QVERIFY(absent && absent->kind == Kind::Unavailable);
    QVERIFY(!client.poll(start + 100ms));
    audio->present = true;
    const auto ready = client.poll(start + 200ms);
    QVERIFY(ready && ready->kind == Kind::Ready);
    client.enable(false);
    const auto reads = audio->reads;
    QVERIFY(!client.poll(start + 300ms));
    QCOMPARE(audio->reads, reads);
    QCOMPARE(audio->writes, 0);
    client.enable(true);
    const auto reconnect = client.poll(start + 400ms);
    QVERIFY(reconnect && reconnect->epoch != ready->epoch);
    QCOMPARE(client.receive({Kind::Set, ready->epoch, 1, {9000, false}, Volume}), ReceiveResult::Ignored);
  }

  void repeatedIncomingUpdatesCannotPostponeRecoveryForever()
  {
    auto control = std::make_unique<FakeVolumeControl>();
    auto *audio = control.get();
    ClientSession client(std::move(control));
    client.enable(true);
    const auto start = Clock::now();
    const auto ready = client.poll(start);
    QVERIFY(ready);
    Message update{Kind::Set, ready->epoch, 1, {2500, false}, Volume | Mute};
    QCOMPARE(client.receive(update, start), ReceiveResult::Applied);
    audio->failWrites = true;
    update.sequence = 2;
    QCOMPARE(client.receive(update, start + 100ms), ReceiveResult::Failed);
    update.sequence = 3;
    QCOMPARE(client.receive(update, start + 1000ms), ReceiveResult::Failed);
    const auto retry = client.poll(start + 1100ms);
    QVERIFY(retry && retry->kind == Kind::Ready && retry->epoch != ready->epoch);
  }

  void peerCoalescesComponentsAndRejectsStaleGenerations()
  {
    PeerSession peer;
    Message ready{Kind::Ready, 7, 0, {5000, false}, Volume | Mute};
    QVERIFY(!peer.receive(ready));
    peer.reset(true);
    QVERIFY(peer.receive(ready));
    QVERIFY(!peer.initialized());
    QVERIFY(peer.receive({Kind::Change, 7, 1, {8000, false}, Volume}));
    QVERIFY(!peer.hasChange());
    QVERIFY(peer.set({2500, false}, Volume | Mute));
    QVERIFY(peer.receive({Kind::Change, 7, 1, {8000, false}, Volume}));
    QVERIFY(peer.receive({Kind::Change, 7, 2, {8000, true}, Mute}));
    QVERIFY(peer.receive({Kind::Change, 7, 3, {9000, true}, Volume}));
    const auto value = peer.takeChange();
    QVERIFY(value);
    QCOMPARE(value->state.volume, uint16_t(9000));
    QVERIFY(value->state.muted);
    QCOMPARE(value->mask, uint8_t(Volume | Mute));
    QVERIFY(peer.receive({Kind::Change, 7, 2, {1000, false}, Volume}));
    QVERIFY(!peer.hasChange());
    ready.epoch = 8;
    QVERIFY(peer.receive(ready));
    QVERIFY(peer.receive({Kind::Change, 7, 4, {1000, false}, Volume}));
    QVERIFY(!peer.hasChange());
    QVERIFY(!peer.initialized());
  }

  void boundedProtocolAndSequenceWrap()
  {
    QVERIFY(decode(1, 1, 0, 10000, 3));
    QVERIFY(!decode(4, 1, 0, 10000, 3));
    QVERIFY(!decode(1, 0, 0, 10000, 3));
    QVERIFY(!decode(1, 1, 0, 10001, 3));
    QVERIFY(!decode(1, 1, 0, 10000, 8));
    QVERIFY(!decode(1, 1, 0, 10000, 2));
    QVERIFY(!decode(3, 1, 0, 10000, 3));
    QVERIFY(!decode(0, 1, 0, 10000, 0));
    QVERIFY(newer(1, UINT32_MAX));
    QVERIFY(!newer(UINT32_MAX, 1));
    QVERIFY(!newer(2, 2));
    QCOMPARE(next(UINT32_MAX), uint32_t(1));
    PeerSession peer;
    peer.reset(true);
    QVERIFY(peer.receive({Kind::Ready, 1, 0, {2000, false}, Volume}));
    QVERIFY(peer.set({2000, false}, Volume));
    QVERIFY(!peer.receive({Kind::Change, 1, 1, {2000, true}, Mute}));
  }
};

QTEST_MAIN(VolumeSyncTests)
#include "VolumeSyncTests.moc"
