/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "IpcClient.h"

#include <QJsonObject>
#include <QObject>

namespace deskflow::gui::ipc {

class CoreIpcClient : public IpcClient
{
  Q_OBJECT

public:
  explicit CoreIpcClient(QObject *parent = nullptr);

  void sendStop();
  void registerAlignmentUi();
  void sendAlignmentMessage(const QJsonObject &message);

Q_SIGNALS:
  void commandReceived(const QString &command, const QString &args);
  void alignmentRegistered();
  void alignmentMessageReceived(const QJsonObject &message);

protected:
  void processCommand(const QString &command, const QStringList &parts) override;

private:
  bool m_alignmentRegistered = false;
  QString m_alignmentLocalName;
};

} // namespace deskflow::gui::ipc
