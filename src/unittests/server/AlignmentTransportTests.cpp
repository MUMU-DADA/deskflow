/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "AlignmentTransportTests.h"

#include "../deskflow/MockEventQueue.h"
#include "common/AlignmentProtocol.h"
#include "common/Settings.h"
#include "common/VersionInfo.h"
#include "deskflow/DeskflowException.h"
#include "deskflow/OptionTypes.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "deskflow/ipc/AlignmentEvent.h"
#include "deskflow/ipc/CoreIpcServer.h"
#include "io/IStream.h"
#include "server/AlignmentSession.h"
#include "server/ClientProxy1_0.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>
#include <cstring>

namespace {
QJsonObject showMessage()
{
  return {
      {"type", "show"},
      {"session", "23456789-abcd-4567-89ab-0123456789ab"},
      {"target", "remote"},
      {"edge", "left"},
      {"points", QJsonArray{0.2, 0.8, 0.5}},
      {"check", false},
      {"revision", 1}
  };
}

class MemoryStream : public deskflow::IStream
{
public:
  QByteArray input;
  QByteArray output;
  uint32_t consumed = 0;
  void close() override
  {
  }
  uint32_t read(void *buffer, uint32_t maximum) override
  {
    const auto size = std::min<uint32_t>(maximum, input.size());
    if (buffer)
      std::memcpy(buffer, input.constData(), size);
    input.remove(0, size);
    consumed += size;
    return size;
  }
  void write(const void *buffer, uint32_t size) override
  {
    output.append(static_cast<const char *>(buffer), size);
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
    return const_cast<MemoryStream *>(this);
  }
  bool isReady() const override
  {
    return !input.isEmpty();
  }
  uint32_t getSize() const override
  {
    return input.size();
  }
};

class CaptureEvents : public MockEventQueue
{
public:
  uint64_t generation = 0;
  bool available = false;
  QList<QJsonObject> peerMessages;
  void addEvent(Event &&event) override
  {
    if (const auto *data = dynamic_cast<AlignmentEventData *>(event.getDataObject())) {
      if (data->kind == AlignmentEventData::Kind::UiAvailability) {
        generation = data->generation;
        available = data->available;
      } else if (event.getType() == EventTypes::AlignmentPeer) {
        peerMessages.append(data->message);
      }
    }
    Event::deleteData(event);
  }
};

class TestProxy : public ClientProxy1_0
{
public:
  TestProxy(MemoryStream *stream, IEventQueue *events) : ClientProxy1_0("actual-peer", stream, events)
  {
  }
  bool receiveCalibration()
  {
    return parseMessage(reinterpret_cast<const uint8_t *>("DCAL"));
  }
};

QByteArray wirePayload(const QJsonObject &message)
{
  MemoryStream stream;
  const auto encoded = deskflow::alignment::encode(message, true).toStdString();
  ProtocolUtil::writef(&stream, kMsgDAlignment, &encoded);
  return stream.output.mid(4); // parseMessage has already consumed the opcode.
}
} // namespace

void AlignmentTransportTests::codecValidation()
{
  const auto message = showMessage();
  const auto json = deskflow::alignment::encode(message);
  QVERIFY(!json.isEmpty());
  QCOMPARE(*deskflow::alignment::decode(json), message);
  const auto ipc = deskflow::alignment::encodeIpc(message);
  QVERIFY(!ipc.contains('='));
  QCOMPARE(*deskflow::alignment::decodeIpc(ipc), message);
  auto malformed = message;
  malformed["points"] = QJsonArray{0.2, 1.1};
  QVERIFY(deskflow::alignment::encode(malformed).isEmpty());
  malformed = message;
  malformed["revision"] = 1.5;
  QVERIFY(deskflow::alignment::encode(malformed).isEmpty());
  malformed = message;
  malformed["session"] = "not-a-uuid";
  QVERIFY(deskflow::alignment::encode(malformed).isEmpty());
  malformed = message;
  malformed["padding"] = QString(9000, 'x');
  QVERIFY(deskflow::alignment::encode(malformed).isEmpty());
  QVERIFY(!deskflow::alignment::decodeIpc(QString(20000, 'x')));
}

void AlignmentTransportTests::realProxyNegotiation()
{
  CaptureEvents events;
  auto *stream = new MemoryStream;
  TestProxy proxy(stream, &events); // Owns stream, executes production proxy methods.
  stream->output.clear();
  QVERIFY(!proxy.sendAlignmentMessage(showMessage()));
  QVERIFY(stream->output.isEmpty());
  proxy.setOptions({kOptionAlignmentCalibration, 1});
  QVERIFY(stream->output.startsWith("DSOP"));
  stream->output.clear();
  QVERIFY(!proxy.alignmentCapable());
  QVERIFY(!proxy.sendAlignmentMessage(showMessage())); // Legacy/no ACK: zero unknown packets sent.
  QVERIFY(stream->output.isEmpty());

  stream->input = wirePayload({{"type", "capability"}, {"version", 1}, {"uiReady", true}});
  QVERIFY(proxy.receiveCalibration());
  QVERIFY(proxy.alignmentCapable());
  QVERIFY(proxy.alignmentUiReady());
  QCOMPARE(events.peerMessages.last().value("source").toString(), QString("actual-peer"));
  QVERIFY(proxy.sendAlignmentMessage(showMessage()));
  QVERIFY(stream->output.startsWith("DCAL"));

  auto points = showMessage();
  points["type"] = "points";
  points["seq"] = 1;
  points["dragging"] = true;
  points["source"] = "spoofed-peer";
  stream->input = wirePayload(points);
  QVERIFY(proxy.receiveCalibration());
  QCOMPARE(events.peerMessages.last().value("source").toString(), QString("actual-peer"));

  proxy.resetOptions();
  stream->output.clear();
  QVERIFY(!proxy.alignmentCapable());
  QVERIFY(!proxy.sendAlignmentMessage(showMessage()));
  QVERIFY(stream->output.isEmpty());
  stream->input = wirePayload({{"type", "capability"}, {"version", 1}, {"uiReady", true}});
  QVERIFY_EXCEPTION_THROWN(proxy.receiveCalibration(), BadClientException);
}

void AlignmentTransportTests::boundedNetworkRead()
{
  MemoryStream stream;
  ProtocolUtil::writef(&stream, "%4i", uint32_t(deskflow::alignment::maxMessageBytes + 1));
  stream.input = stream.output + QByteArray(32, 'x');
  std::string output;
  QVERIFY_EXCEPTION_THROWN(
      ProtocolUtil::readStringWithLimit(&stream, output, deskflow::alignment::maxMessageBytes), BadClientException
  );
  QCOMPARE(stream.consumed, uint32_t(4)); // Rejected at length, before reading/allocating the alleged payload.
  QVERIFY(output.empty());
}

void AlignmentTransportTests::registeredUiGenerationAndFraming()
{
  CaptureEvents events;
  const auto name = "dfcal-" + QUuid::createUuid().toString(QUuid::Id128);
  deskflow::core::ipc::CoreIpcServer server(nullptr, &events, name);
  server.listen(); // Isolated random name, never the live deskflow-core endpoint.
  server.alignmentCoreReady("actual-local", "server");
  const auto hello = QString("hello=%1+%2").arg(kVersion, kVersionGitSha).toUtf8();
  QLocalSocket first;
  first.connectToServer(name);
  QTRY_COMPARE(first.state(), QLocalSocket::ConnectedState);
  first.write("alignmentRegister=1\n");
  QByteArray received;
  QTRY_VERIFY((received += first.readAll()).contains("alignmentRegistered=0\n"));
  QVERIFY(!events.available);
  received.clear();
  first.write(hello); // Deliberately fragmented line: no newline yet.
  first.flush();
  QTest::qWait(20);
  QVERIFY(!events.available);
  first.write("\nalignmentRegister=1\n");
  QTRY_VERIFY((received += first.readAll()).contains("alignmentRegistered=1\n"));
  QTRY_VERIFY(events.available);
  const auto oldGeneration = events.generation;
  server.deliverAlignment(showMessage(), oldGeneration);
  QTRY_VERIFY(first.bytesAvailable() > 0);
  QVERIFY(first.readAll().startsWith("alignment="));
  first.disconnectFromServer();
  QTRY_COMPARE(first.state(), QLocalSocket::UnconnectedState);
  QTRY_VERIFY(!events.available);

  QLocalSocket second;
  second.connectToServer(name);
  QTRY_COMPARE(second.state(), QLocalSocket::ConnectedState);
  second.write(hello + "\nalignmentRegister=1\n");
  received.clear();
  QTRY_VERIFY((received += second.readAll()).contains("alignmentRegistered=1\n"));
  QTRY_VERIFY(events.available);
  QVERIFY(events.generation > oldGeneration);
  server.deliverAlignment(showMessage(), oldGeneration);
  QTest::qWait(20);
  QCOMPARE(second.bytesAvailable(), qint64(0));
  server.deliverAlignment(showMessage(), events.generation);
  QTRY_VERIFY(second.bytesAvailable() > 0);
  QVERIFY(second.readAll().startsWith("alignment="));
  second.write(QByteArray(deskflow::alignment::maxIpcLineBytes + 1, 'x'));
  QTRY_COMPARE(second.state(), QLocalSocket::UnconnectedState);
  QTRY_VERIFY(!events.available);
}

void AlignmentTransportTests::boundedDragLeases()
{
  using namespace std::chrono_literals;
  AlignmentSession session;
  const auto now = AlignmentSession::Clock::now();
  QVERIFY(session.join("id", "first", now));
  QVERIFY(session.join("id", "second", now));
  session.mouseDown(1, "first", now);
  QVERIFY(session.blocks("first", now));
  QVERIFY(!session.blocks("second", now));
  session.touch(now + 2s); // Pings keep session alive, never extend a missing button-up hold.
  QVERIFY(!session.blocks("first", now + 3100ms));
  QVERIFY(!session.expired(now + 3100ms));
  QVERIFY(session.point("first", 1, true, now + 4s));
  QVERIFY(session.point("first", 2, true, now + 6s));
  QVERIFY(session.blocks("first", now + 8s));
  QVERIFY(!session.point("first", 1, false, now + 8s)); // Reordered update cannot release a current drag.
  QVERIFY(session.point("first", 3, false, now + 8s));
  QVERIFY(!session.blocks("first", now + 8s));
  session.mouseDown(1, "first", now + 9s);
  QVERIFY(session.point("first", 4, false, now + 9s)); // Late GUI release cannot erase a newer native down.
  QVERIFY(session.blocks("first", now + 9s));
  QVERIFY(session.point("first", 5, true, now + 9s));
  session.mouseUp(1);
  QVERIFY(!session.blocks("first", now + 9s));
  session.clear();
  QVERIFY(session.id.isEmpty());
  QVERIFY(!session.blocks("first", now + 9s));
}

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  static QTemporaryDir isolated;
  if (!isolated.isValid())
    return 2;
  QDir().mkpath(isolated.path() + "/config/Deskflow");
  QDir().mkpath(isolated.path() + "/state");
  qputenv("XDG_CONFIG_HOME", (isolated.path() + "/config").toUtf8());
  qputenv("XDG_STATE_HOME", (isolated.path() + "/state").toUtf8());
#ifdef Q_OS_WIN
  const QString bootstrap = QCoreApplication::applicationDirPath() + "/settings/Deskflow.conf";
  QDir().mkpath(QFileInfo(bootstrap).absolutePath());
  QFile seed(bootstrap);
  if (!seed.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    return 2;
  seed.close();
#endif
  Settings::setSettingsFile(isolated.path() + "/config/Deskflow/Deskflow.conf");
  Settings::setStateFile(isolated.path() + "/state/Deskflow.state");
  AlignmentTransportTests tests;
  const auto result = QTest::qExec(&tests, argc, argv);
  Settings::save(false);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
#ifdef Q_OS_WIN
  QFile::remove(bootstrap);
#endif
  return result;
}
