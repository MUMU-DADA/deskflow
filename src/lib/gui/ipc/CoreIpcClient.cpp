/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcClient.h"

#include "common/AlignmentProtocol.h"
#include "common/Constants.h"

#include <QString>

namespace deskflow::gui::ipc {

CoreIpcClient::CoreIpcClient(QObject *parent) : IpcClient(parent, kCoreIpcName, QStringLiteral("core"))
{
  connect(this, &IpcClient::disconnected, this, [this] { m_alignmentRegistered = false; });
  connect(this, &IpcClient::versionMismatch, this, [this] { m_alignmentRegistered = false; });
}

void CoreIpcClient::sendStop()
{
  sendMessage(QStringLiteral("stop"));
}

void CoreIpcClient::processCommand(const QString &command, const QStringList &parts)
{
  if (command == "alignmentRegistered") {
    m_alignmentRegistered = parts.value(1) == "1";
    if (m_alignmentRegistered)
      Q_EMIT alignmentRegistered();
    return;
  }
  if (command == "alignment") {
    if (const auto message = deskflow::alignment::decodeIpc(parts.value(1))) {
      if (message->value("type") == "capabilities")
        m_alignmentLocalName = message->value("source").toString();
      Q_EMIT alignmentMessageReceived(*message);
    }
    return;
  }
  const auto args = parts.size() >= 2 ? parts.at(1) : QString();
  Q_EMIT commandReceived(command, args);
}

void CoreIpcClient::registerAlignmentUi()
{
  m_alignmentRegistered = false;
  sendMessage("alignmentRegister=1");
}

void CoreIpcClient::sendAlignmentMessage(const QJsonObject &message)
{
  if (!m_alignmentRegistered || !isConnected()) {
    Q_EMIT alignmentMessageReceived(deskflow::alignment::error(message, m_alignmentLocalName, "not_ready"));
    return;
  }
  auto outgoing = message;
  outgoing.remove("source");
  const auto data = deskflow::alignment::encodeIpc(outgoing);
  if (data.isEmpty() || !deskflow::alignment::validMessage(outgoing)) {
    Q_EMIT alignmentMessageReceived(deskflow::alignment::error(message, m_alignmentLocalName, "invalid_message"));
    return;
  }
  sendMessage("alignment=" + data);
}

} // namespace deskflow::gui::ipc
