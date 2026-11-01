/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcServer.h"

#include "AlignmentEvent.h"
#include "CoreIpc.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "common/AlignmentProtocol.h"
#include "common/Constants.h"

#include <QLocalSocket>

namespace deskflow::core::ipc {

static CoreIpcServer *s_instance = nullptr;

CoreIpcServer::CoreIpcServer(QObject *parent, IEventQueue *events, const QString &socketName)
    : IpcServer(parent, socketName, QStringLiteral("core")),
      m_events(events)
{
  assert(s_instance == nullptr);
  s_instance = this;
}

CoreIpcServer::~CoreIpcServer()
{
  m_events = nullptr;
  s_instance = nullptr;
}

CoreIpcServer *CoreIpcServer::maybeInstance()
{
  return s_instance;
}

CoreIpcServer &CoreIpcServer::instance()
{
  assert(s_instance != nullptr);
  return *s_instance;
}

void CoreIpcServer::processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts)
{
  if (command == "alignmentRegister") {
    if (!hasHandshake(clientSocket) || parts.value(1) != "1" || (m_alignmentUi && m_alignmentUi != clientSocket)) {
      writeToClientSocket(clientSocket, "alignmentRegistered=0");
      return;
    }
    if (m_alignmentUi != clientSocket) {
      m_alignmentUi = clientSocket;
      ++m_alignmentGeneration;
    }
    if (m_alignmentCoreReady) {
      writeToClientSocket(clientSocket, "alignmentRegistered=1");
      notifyAlignmentUi();
    }
    return;
  }
  if (command == "alignment") {
    if (!hasHandshake(clientSocket) || m_alignmentUi != clientSocket)
      return;
    auto message = deskflow::alignment::decodeIpc(parts.value(1));
    if (!message)
      return;
    message->remove("source"); // Never trust a GUI-supplied origin.
    if (!deskflow::alignment::validMessage(*message)) {
      sendAlignment(clientSocket, deskflow::alignment::error(*message, m_alignmentName, "invalid_message"));
      return;
    }
    if (!m_alignmentCoreReady || !m_events) {
      sendAlignment(clientSocket, deskflow::alignment::error(*message, m_alignmentName, "not_ready"));
      return;
    }
    m_events->addEvent(Event(
        EventTypes::AlignmentLocal, alignmentEventTarget(),
        new AlignmentEventData(AlignmentEventData::Kind::Message, *message, m_alignmentGeneration)
    ));
    return;
  }
  if (command == QStringLiteral("stop")) {
    LOG_DEBUG("core ipc server got stop message");
    writeToClientSocket(clientSocket, QStringLiteral("ok"));
    broadcastCommand(QStringLiteral("bye"));
    Q_EMIT stopProcessRequested();
    return;
  }
  LOG_WARN("core ipc server got unknown command: %s", command.toUtf8().constData());
}

void CoreIpcServer::clientRemoved(QLocalSocket *clientSocket)
{
  if (m_alignmentUi != clientSocket)
    return;
  m_alignmentUi.clear();
  ++m_alignmentGeneration;
  notifyAlignmentUi();
}

void CoreIpcServer::alignmentCoreReady(const QString &name, const QString &role)
{
  m_alignmentName = name;
  m_alignmentRole = role;
  m_alignmentCoreReady = true;
  if (auto *ui = m_alignmentUi.data()) {
    writeToClientSocket(ui, "alignmentRegistered=1");
    ui->flush();
  }
  notifyAlignmentUi();
}

void CoreIpcServer::alignmentCoreStopped()
{
  m_alignmentCoreReady = false;
  ++m_alignmentGeneration;
}

void CoreIpcServer::notifyAlignmentUi()
{
  if (m_alignmentCoreReady && m_events)
    m_events->addEvent(Event(
        EventTypes::AlignmentLocal, alignmentEventTarget(),
        new AlignmentEventData(
            AlignmentEventData::Kind::UiAvailability, {}, m_alignmentGeneration, !m_alignmentUi.isNull()
        )
    ));
}

void CoreIpcServer::sendAlignment(QLocalSocket *clientSocket, const QJsonObject &message)
{
  const auto data = deskflow::alignment::encodeIpc(message);
  if (!data.isEmpty()) {
    writeToClientSocket(clientSocket, "alignment=" + data);
    clientSocket->flush();
  }
}

void CoreIpcServer::deliverAlignment(const QJsonObject &message, uint64_t generation)
{
  // Ephemeral overlay traffic is never put in IpcServer's pending broadcast
  // queue, and cannot be replayed into a replacement GUI connection.
  if (m_alignmentCoreReady && m_alignmentUi && generation == m_alignmentGeneration)
    sendAlignment(m_alignmentUi.data(), message);
}

} // namespace deskflow::core::ipc
