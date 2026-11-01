/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "IpcServer.h"
#include "common/Constants.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>

class QLocalSocket;
class IEventQueue;

namespace deskflow::core::ipc {

class CoreIpcServer : public IpcServer
{
  Q_OBJECT

public:
  explicit CoreIpcServer(QObject *parent, IEventQueue *events = nullptr, const QString &socketName = kCoreIpcName);
  ~CoreIpcServer() override;

  static CoreIpcServer &instance();
  static CoreIpcServer *maybeInstance();
  // These methods are invoked only on this QObject's main thread.
  void alignmentCoreReady(const QString &name, const QString &role);
  void alignmentCoreStopped();
  void deliverAlignment(const QJsonObject &message, uint64_t generation);

private:
  void processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts) override;
  void clientRemoved(QLocalSocket *clientSocket) override;
  void notifyAlignmentUi();
  void sendAlignment(QLocalSocket *clientSocket, const QJsonObject &message);

  IEventQueue *m_events = nullptr;
  QPointer<QLocalSocket> m_alignmentUi;
  uint64_t m_alignmentGeneration = 0;
  bool m_alignmentCoreReady = false;
  QString m_alignmentName;
  QString m_alignmentRole;
};

} // namespace deskflow::core::ipc
