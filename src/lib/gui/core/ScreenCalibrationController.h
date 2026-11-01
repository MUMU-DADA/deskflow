/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "gui/config/ScreenAlignment.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QWidget;
class ScreenCalibrationOverlay;

struct ScreenCalibrationEndpointState
{
  QList<double> points{0.25, 0.75, 0.5};
  QSizeF logicalSize;
  double devicePixelRatio = 1;
  bool ready = false;
  qint64 sequence = -1;
};

// Owns one temporary calibration session and the local edge markers. It never
// changes saved settings or active links; the enclosing configuration dialog
// decides whether to commit a completed result.
class ScreenCalibrationController : public QObject
{
  Q_OBJECT
public:
  explicit ScreenCalibrationController(QWidget *window);
  ~ScreenCalibrationController() override;

  bool start(const ScreenAlignment &alignment, QWidget *overlayOwner);
  void cancel();
  void setCheckPointEnabled(bool enabled);
  bool checkPointEnabled() const
  {
    return m_checkEnabled;
  }
  bool isAvailable() const
  {
    return m_available;
  }
  bool isActive() const
  {
    return !m_session.isEmpty();
  }
  bool canAccept() const;
  std::optional<ScreenEdgeCalibration> result() const;
  QString statusText() const;
  QString measurementText() const;
  ScreenCalibrationEndpointState firstEndpoint() const
  {
    return m_first;
  }
  ScreenCalibrationEndpointState secondEndpoint() const
  {
    return m_second;
  }

public Q_SLOTS:
  void setTransportAvailable(bool available);
  void receiveMessage(const QJsonObject &message);

Q_SIGNALS:
  void sendMessage(const QJsonObject &message);
  void stateChanged();
  void cancelled(const QString &reason);

private:
  void sendTo(const QString &target, const QString &type, QJsonObject payload = {});
  void showEndpoints();
  void updateOwner(const QJsonObject &message);
  void showLocal(const QJsonObject &message);
  void reportLocalReady();
  void sendLocalPoints();
  void stopLocal();
  void abortSession(const QString &reason);
  void replyLocal(const QString &type, QJsonObject payload = {});
  static QString errorText(const QString &reason);

  QPointer<QWidget> m_window;
  QPointer<QWidget> m_owner;
  bool m_available = false;
  QString m_session;
  QString m_transportName;
  qint64 m_revision = 0;
  ScreenAlignment m_alignment;
  ScreenCalibrationEndpointState m_first;
  ScreenCalibrationEndpointState m_second;
  bool m_checkEnabled = false;
  QString m_problem;
  QTimer m_heartbeat;
  QTimer m_handshakeTimeout;
  QTimer m_sessionTimeout;

  QPointer<ScreenCalibrationOverlay> m_overlay;
  QString m_localSession;
  QString m_localOrigin;
  QList<double> m_localPoints;
  qint64 m_localSequence = 0;
  qint64 m_localRevision = 0;
  bool m_localDragging = false;
  QTimer m_localLease;
  QTimer m_dragHeartbeat;
};
