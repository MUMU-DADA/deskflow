/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenCalibrationController.h"

#include "gui/widgets/ScreenCalibrationOverlay.h"

#include <QApplication>
#include <QDebug>
#include <QJsonArray>
#include <QScreen>
#include <QUuid>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace {
std::optional<QList<double>> readPoints(const QJsonValue &value)
{
  if (!value.isArray())
    return std::nullopt;
  const auto array = value.toArray();
  if (array.size() < 2 || array.size() > 3)
    return std::nullopt;
  QList<double> points;
  for (const auto &entry : array) {
    if (!entry.isDouble() || !std::isfinite(entry.toDouble()) || entry.toDouble() < 0 || entry.toDouble() > 1)
      return std::nullopt;
    points.append(entry.toDouble());
  }
  if (points.size() == 2)
    points.append((points[0] + points[1]) / 2);
  return points;
}

QJsonArray jsonPoints(const QList<double> &points)
{
  QJsonArray result;
  for (double point : points)
    result.append(point);
  return result;
}

double checkTolerance(
    const ScreenEdgeCalibration &calibration, const ScreenCalibrationEndpointState &first,
    const ScreenCalibrationEndpointState &second, Qt::Orientation orientation
)
{
  const double firstExtent = orientation == Qt::Horizontal ? first.logicalSize.height() : first.logicalSize.width();
  const double secondExtent = orientation == Qt::Horizontal ? second.logicalSize.height() : second.logicalSize.width();
  const auto transform = calibration.transform();
  if (!transform || firstExtent <= 0 || secondExtent <= 0)
    return 0;
  // Allow two logical pixels at either endpoint, or 0.3% of the first edge.
  // This is a check tolerance, never a physical millimeter measurement.
  return std::max(0.003, 2 / firstExtent + 2 * transform->ratio / secondExtent);
}
} // namespace

ScreenCalibrationController::ScreenCalibrationController(QWidget *window) : QObject(window), m_window(window)
{
  m_heartbeat.setInterval(1000);
  connect(&m_heartbeat, &QTimer::timeout, this, [this] {
    sendTo(m_alignment.firstName, "ping");
    sendTo(m_alignment.secondName, "ping");
  });
  m_handshakeTimeout.setSingleShot(true);
  m_handshakeTimeout.setInterval(10000);
  connect(&m_handshakeTimeout, &QTimer::timeout, this, [this] {
    abortSession(
        tr("The other computer did not show its markers. Check that both computers run the updated Deskflow GUI.")
    );
  });
  m_sessionTimeout.setSingleShot(true);
  m_sessionTimeout.setInterval(5 * 60 * 1000);
  connect(&m_sessionTimeout, &QTimer::timeout, this, [this] {
    abortSession(tr("Calibration timed out. Start it again when you are ready."));
  });
  m_localLease.setSingleShot(true);
  m_localLease.setInterval(10000);
  connect(&m_localLease, &QTimer::timeout, this, [this] {
    replyLocal("cancel", {{"reason", "timeout"}});
    stopLocal();
  });
  m_dragHeartbeat.setInterval(250);
  connect(&m_dragHeartbeat, &QTimer::timeout, this, &ScreenCalibrationController::sendLocalPoints);
}

ScreenCalibrationController::~ScreenCalibrationController()
{
  cancel();
  stopLocal();
}

bool ScreenCalibrationController::start(const ScreenAlignment &alignment, QWidget *overlayOwner)
{
  if (isActive())
    return false;
  if (!m_localSession.isEmpty()) {
    m_problem = tr("Another screen calibration is already running.");
    Q_EMIT stateChanged();
    return false;
  }
  m_problem.clear();
  if (!m_available) {
    m_problem = tr("Start Deskflow and connect both computers before calibrating their edges.");
    Q_EMIT stateChanged();
    return false;
  }
  m_alignment = alignment;
  m_owner = overlayOwner;
  m_first = {};
  m_second = {};
  m_checkEnabled = false;
  m_revision = 0;
  if (alignment.calibration) {
    const auto &calibration = *alignment.calibration;
    m_first.points = {calibration.a.first, calibration.b.first, (calibration.a.first + calibration.b.first) / 2};
    m_second.points = {calibration.a.second, calibration.b.second, (calibration.a.second + calibration.b.second) / 2};
  }
  m_session = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_heartbeat.start();
  m_handshakeTimeout.start();
  m_sessionTimeout.start();
  showEndpoints();
  Q_EMIT stateChanged();
  return isActive();
}

void ScreenCalibrationController::cancel()
{
  m_heartbeat.stop();
  m_handshakeTimeout.stop();
  m_sessionTimeout.stop();
  if (!m_session.isEmpty()) {
    sendTo(m_alignment.firstName, "close");
    sendTo(m_alignment.secondName, "close");
    if (m_localSession == m_session)
      stopLocal();
  }
  m_session.clear();
  m_owner.clear();
  m_first.ready = false;
  m_second.ready = false;
  Q_EMIT stateChanged();
}

void ScreenCalibrationController::abortSession(const QString &reason)
{
  cancel();
  m_problem = reason;
  Q_EMIT stateChanged();
  Q_EMIT cancelled(reason);
}

void ScreenCalibrationController::setTransportAvailable(bool available)
{
  m_available = available;
  if (!available) {
    if (isActive())
      abortSession(tr("The Deskflow connection was interrupted. Your previous alignment has been kept."));
    stopLocal();
  }
  Q_EMIT stateChanged();
}

void ScreenCalibrationController::sendTo(const QString &target, const QString &type, QJsonObject payload)
{
  if (!m_available || m_session.isEmpty())
    return;
  payload.insert("session", m_session);
  payload.insert("target", target);
  payload.insert("type", type);
  Q_EMIT sendMessage(payload);
}

void ScreenCalibrationController::showEndpoints()
{
  ++m_revision;
  const bool horizontal = m_alignment.orientation == Qt::Horizontal;
  sendTo(
      m_alignment.firstName, "show",
      {{"edge", horizontal ? "right" : "bottom"},
       {"points", jsonPoints(m_first.points)},
       {"check", m_checkEnabled},
       {"revision", m_revision}}
  );
  sendTo(
      m_alignment.secondName, "show",
      {{"edge", horizontal ? "left" : "top"},
       {"points", jsonPoints(m_second.points)},
       {"check", m_checkEnabled},
       {"revision", m_revision}}
  );
}

void ScreenCalibrationController::setCheckPointEnabled(bool enabled)
{
  if (m_checkEnabled == enabled || !isActive())
    return;
  m_checkEnabled = enabled;
  m_first.ready = false;
  m_second.ready = false;
  m_handshakeTimeout.start();
  showEndpoints();
  Q_EMIT stateChanged();
}

std::optional<ScreenEdgeCalibration> ScreenCalibrationController::result() const
{
  if (!isActive() || !m_first.ready || !m_second.ready)
    return std::nullopt;
  ScreenEdgeCalibration calibration{
      {m_first.points[0], m_second.points[0]},
      {m_first.points[1], m_second.points[1]},
      m_first.logicalSize.width() / m_first.logicalSize.height(),
      m_second.logicalSize.width() / m_second.logicalSize.height()
  };
  return calibration.error() == ScreenEdgeCalibration::Error::None ? std::optional(calibration) : std::nullopt;
}

bool ScreenCalibrationController::canAccept() const
{
  const auto calibration = result();
  if (!calibration)
    return false;
  if (!m_checkEnabled)
    return true;
  const auto residual = calibration->validationResidual({m_first.points[2], m_second.points[2]});
  return residual && std::abs(*residual) <= checkTolerance(*calibration, m_first, m_second, m_alignment.orientation);
}

QString ScreenCalibrationController::statusText() const
{
  if (!m_problem.isEmpty())
    return m_problem;
  if (!isActive())
    return tr("Calibration is not running.");
  if (!m_first.ready || !m_second.ready)
    return tr("Waiting for both computers to display their edge markers…");
  ScreenEdgeCalibration calibration{{m_first.points[0], m_second.points[0]}, {m_first.points[1], m_second.points[1]}};
  if (calibration.error() == ScreenEdgeCalibration::Error::PointsTooClose)
    return tr("Move A and B farther apart on both screens (at least 10% of each edge).");
  if (calibration.error() == ScreenEdgeCalibration::Error::Reversed)
    return tr("Keep A and B in the same order on both screens.");
  if (!canAccept())
    return m_checkEnabled && result()
               ? tr("The third point does not match. Recheck A, B and C before using this calibration.")
               : tr("These marker positions cannot produce a usable alignment.");
  return tr("Align each matching color in the real world, then choose Use calibration.");
}

QString ScreenCalibrationController::measurementText() const
{
  const auto calibration = result();
  if (!calibration)
    return {};
  const auto transform = *calibration->transform();
  QString text = tr("Relative edge length: %1 : %2\nSecond-screen offset: %3% of the first edge. No millimeter "
                    "measurement is required.")
                     .arg(
                         QStringLiteral("1"), QString::number(transform.ratio, 'g', 5),
                         QString::number(100 * transform.offset, 'f', 2)
                     );
  if (m_checkEnabled) {
    const auto residual = calibration->validationResidual({m_first.points[2], m_second.points[2]});
    if (residual)
      text += '\n' + tr("Third-point discrepancy: %1% of the first edge.").arg(100 * std::abs(*residual), 0, 'f', 2);
  }
  return text;
}

void ScreenCalibrationController::receiveMessage(const QJsonObject &message)
{
  if (message.value("type").toString() == "capabilities") {
    m_transportName = message.value("source").toString();
    return;
  }
  const QString session = message.value("session").toString();
  if (session.isEmpty())
    return;
  const QString type = message.value("type").toString();
  if (session == m_session && (type == "ready" || type == "points" || type == "error" || type == "cancel"))
    updateOwner(message);
  if (type == "show") {
    showLocal(message);
  } else if (session == m_localSession && (message.value("source").toString() == m_localOrigin ||
                                           (type == "cancel" && !m_transportName.isEmpty() &&
                                            message.value("source").toString() == m_transportName))) {
    if (type == "ping")
      m_localLease.start();
    else if (type == "close" || type == "cancel")
      stopLocal();
  }
}

void ScreenCalibrationController::updateOwner(const QJsonObject &message)
{
  const QString type = message.value("type").toString();
  const QString source = message.value("source").toString();
  if (source != m_alignment.firstName && source != m_alignment.secondName &&
      (m_transportName.isEmpty() || source != m_transportName))
    return;
  if (type == "error" || type == "cancel") {
    abortSession(errorText(message.value("reason").toString()));
    return;
  }
  if (message.value("revision").toInteger(-1) != m_revision)
    return;
  auto *endpoint = source == m_alignment.firstName ? &m_first : source == m_alignment.secondName ? &m_second : nullptr;
  const auto points = readPoints(message.value("points"));
  if (!endpoint || !points || (m_checkEnabled && message.value("points").toArray().size() != 3))
    return;
  const double sequence = message.value("seq").toDouble(-1);
  if (!std::isfinite(sequence) || sequence < 0 || sequence > 9007199254740991.0 || std::floor(sequence) != sequence ||
      sequence <= endpoint->sequence)
    return;
  if (type == "ready") {
    const auto screen = message.value("screen").toObject();
    const double width = screen.value("width").toDouble();
    const double height = screen.value("height").toDouble();
    const double dpr = screen.value("dpr").toDouble();
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(dpr) || width < 2 || height < 2 ||
        width > 100000 || height > 100000 || dpr <= 0 || dpr > 16 || screen.value("screenCount").toInt() != 1) {
      abortSession(
          tr("The display geometry changed or is unsupported. Restart calibration on a single display per computer.")
      );
      return;
    }
    if (endpoint->logicalSize.width() > 0 &&
        (endpoint->logicalSize != QSizeF(width, height) || endpoint->devicePixelRatio != dpr)) {
      abortSession(
          tr("The display geometry changed or is unsupported. Restart calibration on a single display per computer.")
      );
      return;
    }
    endpoint->logicalSize = {width, height};
    endpoint->devicePixelRatio = dpr;
    endpoint->ready = true;
  }
  endpoint->points = *points;
  endpoint->sequence = static_cast<qint64>(sequence);
  if (m_first.ready && m_second.ready)
    m_handshakeTimeout.stop();
  Q_EMIT stateChanged();
}

QString ScreenCalibrationController::errorText(const QString &reason)
{
  if (reason == "unsupported")
    return tr("Update Deskflow on both computers to use on-screen edge markers. Manual alignment remains available.");
  if (reason == "ui_unavailable" || reason == "not_ready")
    return tr("Open the updated Deskflow GUI on both computers, connect them, then try again.");
  if (reason == "ui_busy" || reason == "busy")
    return tr("The other computer has an open dialog or calibration. Close it and try again.");
  if (reason == "escape" || reason == "user_cancelled")
    return tr("Calibration was cancelled. Your previous alignment has been kept.");
  if (reason == "wayland")
    return tr("This Wayland session cannot position edge markers precisely. Use manual screen alignment here.");
  if (reason == "multiple_displays")
    return tr("Edge markers currently require one display per computer. Use manual alignment for other layouts.");
  if (reason == "display_changed")
    return tr("A display's resolution, rotation or scaling changed. Start calibration again.");
  if (reason == "marker_repositioned")
    return tr("The system moved an edge marker away from its calibrated position. Retry, or use manual alignment.");
  if (reason == "marker_unavailable" || reason == "invalid_positions")
    return tr("The edge markers could not be placed accurately. Retry, or use manual alignment.");
  return tr(
      "Calibration stopped because a computer or its display is no longer available. Your previous alignment has been "
      "kept."
  );
}

void ScreenCalibrationController::showLocal(const QJsonObject &message)
{
  const auto source = message.value("source").toString();
  const auto session = message.value("session").toString();
  const auto points = readPoints(message.value("points"));
  const auto revision = message.value("revision").toInteger(-1);
  const auto fail = [&](const QString &reason) {
    if (source.isEmpty())
      return;
    Q_EMIT sendMessage(QJsonObject{{"session", session}, {"target", source}, {"type", "error"}, {"reason", reason}});
  };
  if (source.isEmpty() || !points || QUuid(session).isNull() || revision < 1 || revision > 2147483647 ||
      (message.value("check").toBool() && message.value("points").toArray().size() != 3)) {
    fail("invalid_request");
    return;
  }
  if (!m_localSession.isEmpty() && (m_localSession != session || m_localOrigin != source)) {
    fail("busy");
    return;
  }
  if (m_localSession == session && revision < m_localRevision)
    return;
  QWidget *parent = session == m_session ? m_owner.data() : m_window.data();
  if (auto *modal = QApplication::activeModalWidget(); modal && modal != parent && !modal->isAncestorOf(parent)) {
    fail("ui_busy");
    return;
  }
  using Edge = ScreenCalibrationOverlay::Edge;
  const auto edgeName = message.value("edge").toString();
  std::optional<Edge> edge;
  if (edgeName == "left")
    edge = Edge::Left;
  else if (edgeName == "right")
    edge = Edge::Right;
  else if (edgeName == "top")
    edge = Edge::Top;
  else if (edgeName == "bottom")
    edge = Edge::Bottom;
  if (!edge) {
    fail("invalid_request");
    return;
  }
  if (!m_overlay) {
    m_overlay = new ScreenCalibrationOverlay(parent);
    connect(
        m_overlay.data(), &ScreenCalibrationOverlay::placementReady, this,
        &ScreenCalibrationController::reportLocalReady
    );
    connect(m_overlay.data(), &ScreenCalibrationOverlay::pointMoved, this, [this](int index, double position) {
      if (index < 0 || index >= m_localPoints.size())
        return;
      m_localPoints[index] = position;
      sendLocalPoints();
    });
    connect(m_overlay.data(), &ScreenCalibrationOverlay::dragStateChanged, this, [this](bool dragging) {
      m_localDragging = dragging;
      dragging ? m_dragHeartbeat.start() : m_dragHeartbeat.stop();
      sendLocalPoints();
    });
    connect(m_overlay.data(), &ScreenCalibrationOverlay::cancelRequested, this, [this] {
      replyLocal("cancel", {{"reason", "user_cancelled"}});
      stopLocal();
    });
    connect(m_overlay.data(), &ScreenCalibrationOverlay::invalidated, this, [this](const QString &reason) {
      const auto code = m_overlay ? m_overlay->failureCode() : QStringLiteral("marker_unavailable");
      qWarning().noquote() << "Screen calibration stopped:" << code << reason;
      replyLocal("error", {{"reason", code}, {"detail", reason}});
      stopLocal();
    });
  }
  const bool newSession = m_localSession != session;
  m_localSession = session;
  m_localOrigin = source;
  m_localPoints = *points;
  m_localRevision = revision;
  if (newSession)
    m_localSequence = 0;
  const auto visiblePoints = message.value("check").toBool() ? m_localPoints : m_localPoints.mid(0, 2);
  QString problem;
  if (m_overlay->isRunning())
    m_overlay->setPositions(visiblePoints);
  else
    problem = m_overlay->showForEdge(*edge, visiblePoints);
  if (!m_overlay) // The invalidated callback already reported the exact failure.
    return;
  if (!problem.isEmpty() || !m_overlay->isRunning()) {
    const auto code = QGuiApplication::platformName().startsWith("wayland") ? QStringLiteral("wayland")
                      : QGuiApplication::screens().size() != 1              ? QStringLiteral("multiple_displays")
                                                                            : m_overlay->failureCode();
    qWarning().noquote() << "Screen calibration could not start:" << code << problem;
    replyLocal("error", {{"reason", code}, {"detail", problem}});
    stopLocal();
    return;
  }
  m_localLease.start();
  // Native creation can reposition a tool window. Wait for verification of the
  // post-show geometry before reporting coordinates as ready.
}

void ScreenCalibrationController::reportLocalReady()
{
  if (!m_overlay || !m_overlay->isRunning() || !m_overlay->isPlacementReady() || m_localSession.isEmpty())
    return;
  const auto *screen = QGuiApplication::primaryScreen();
  if (!screen || QGuiApplication::screens().size() != 1) {
    replyLocal("error", {{"reason", "display_changed"}});
    stopLocal();
    return;
  }
  const auto rendered = m_overlay->positions();
  for (int i = 0; i < rendered.size() && i < m_localPoints.size(); ++i)
    m_localPoints[i] = rendered[i];
  const auto geometry = m_overlay->screenGeometry();
  replyLocal(
      "ready", {{"points", jsonPoints(m_localPoints)},
                {"seq", ++m_localSequence},
                {"revision", m_localRevision},
                {"screen",
                 QJsonObject{
                     {"width", geometry.width()},
                     {"height", geometry.height()},
                     {"dpr", screen->devicePixelRatio()},
                     {"screenCount", 1}
                 }}}
  );
}

void ScreenCalibrationController::replyLocal(const QString &type, QJsonObject payload)
{
  if (!m_available || m_localSession.isEmpty() || m_localOrigin.isEmpty())
    return;
  payload.insert("session", m_localSession);
  payload.insert("target", m_localOrigin);
  payload.insert("type", type);
  Q_EMIT sendMessage(payload);
}

void ScreenCalibrationController::sendLocalPoints()
{
  replyLocal(
      "points", {{"points", jsonPoints(m_localPoints)},
                 {"dragging", m_localDragging},
                 {"seq", ++m_localSequence},
                 {"revision", m_localRevision}}
  );
}

void ScreenCalibrationController::stopLocal()
{
  m_localLease.stop();
  m_dragHeartbeat.stop();
  if (m_localDragging) {
    m_localDragging = false;
    sendLocalPoints();
  }
  // Clear the session before hiding windows: releasing an active mouse grab
  // can synchronously notify the controller again.
  m_localSession.clear();
  m_localOrigin.clear();
  if (m_overlay) {
    auto *overlay = m_overlay.data();
    m_overlay = nullptr;
    disconnect(overlay, nullptr, this, nullptr);
    overlay->stop();
    overlay->deleteLater();
  }
}
