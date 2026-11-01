/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenCalibrationOverlay.h"

#include <QApplication>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSignalBlocker>
#include <QTimer>
#include <QWidget>

#if defined(Q_OS_MACOS)
#include "gui/OSXHelpers.h"
#endif

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace {
using Edge = ScreenCalibrationOverlay::Edge;
using Point = ScreenCalibrationOverlay::Point;

bool sideEdge(Edge edge)
{
  return edge == Edge::Left || edge == Edge::Right;
}

bool validEdge(Edge edge)
{
  return edge == Edge::Left || edge == Edge::Right || edge == Edge::Top || edge == Edge::Bottom;
}

QSize markerSize(Edge edge)
{
  return sideEdge(edge) ? QSize(69, 37) : QSize(37, 69);
}

QPointF markerTip(Edge edge)
{
  const QSize size = markerSize(edge);
  switch (edge) {
  case Edge::Left:
    return {0.5, size.height() / 2.0};
  case Edge::Right:
    return {size.width() - 0.5, size.height() / 2.0};
  case Edge::Top:
    return {size.width() / 2.0, 0.5};
  case Edge::Bottom:
    return {size.width() / 2.0, size.height() - 0.5};
  }
  return {};
}

bool validPositions(const QList<double> &positions)
{
  return (positions.size() == 2 || positions.size() == 3) &&
         std::all_of(positions.begin(), positions.end(), [](double value) {
           return std::isfinite(value) && value >= 0 && value <= 1;
         });
}
} // namespace

class ScreenCalibrationMarker : public QWidget
{
public:
  ScreenCalibrationMarker(Point point, QWidget *owner)
      : QWidget(owner, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus),
        m_point(point)
  {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setFocusPolicy(Qt::NoFocus);
    setObjectName(QStringLiteral("calibrationMarker%1").arg(static_cast<int>(point)));
    setAccessibleName(ScreenCalibrationOverlay::tr("Screen-edge calibration marker %1").arg(label()));
    setToolTip(
        ScreenCalibrationOverlay::tr("Drag %1 along this screen edge. Right-click to cancel calibration.").arg(label())
    );
    setEdge(Edge::Right);
  }

  std::function<void()> dragStarted;
  std::function<void(const QPointF &)> moved;
  std::function<void()> dragEnded;
  std::function<void()> cancel;

  void setEdge(Edge edge)
  {
    if (m_edge == edge && size() == markerSize(edge))
      return;
    m_edge = edge;
    setFixedSize(markerSize(edge));
    setCursor(sideEdge(edge) ? Qt::SizeVerCursor : Qt::SizeHorCursor);
    update();
  }

  void stopDrag()
  {
    m_pressed = false;
    if (QWidget::mouseGrabber() == this)
      releaseMouse();
  }

protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPointF tip = markerTip(m_edge);
    QPainterPath arrow;
    QRectF grip;
    if (sideEdge(m_edge)) {
      const bool left = m_edge == Edge::Left;
      const double base = left ? 19 : width() - 19;
      grip = left ? QRectF(17, 3, width() - 20, height() - 6) : QRectF(3, 3, width() - 20, height() - 6);
      arrow.moveTo(tip);
      arrow.lineTo(base, 1.5);
      arrow.lineTo(base, height() - 1.5);
    } else {
      const bool top = m_edge == Edge::Top;
      const double base = top ? 19 : height() - 19;
      grip = top ? QRectF(3, 17, width() - 6, height() - 20) : QRectF(3, 3, width() - 6, height() - 20);
      arrow.moveTo(tip);
      arrow.lineTo(1.5, base);
      arrow.lineTo(width() - 1.5, base);
    }
    arrow.closeSubpath();
    QPainterPath shape;
    shape.addRoundedRect(grip, 6, 6);
    shape = shape.united(arrow);
    painter.setPen(QPen(QColor(15, 15, 15, 185), 1));
    painter.setBrush(ScreenCalibrationOverlay::pointColor(m_point));
    painter.drawPath(shape);
    QFont text = font();
    text.setBold(true);
    text.setPointSizeF(qMax(13.0, text.pointSizeF()));
    painter.setFont(text);
    painter.setPen(m_point == Point::B ? QColor(25, 25, 25) : Qt::white);
    painter.drawText(grip, Qt::AlignCenter, label());
  }

  void mousePressEvent(QMouseEvent *event) override
  {
    if (event->button() == Qt::RightButton) {
      event->accept();
      cancel();
    } else if (event->button() == Qt::LeftButton) {
      m_pressed = true;
      m_pressPosition = event->globalPosition();
      event->accept();
      dragStarted();
    } else {
      QWidget::mousePressEvent(event);
    }
  }

  void mouseMoveEvent(QMouseEvent *event) override
  {
    if (m_pressed) {
      const auto delta = event->globalPosition() - m_pressPosition;
      event->accept();
      moved(delta);
    } else {
      QWidget::mouseMoveEvent(event);
    }
  }

  void mouseReleaseEvent(QMouseEvent *event) override
  {
    if (event->button() == Qt::LeftButton && m_pressed) {
      stopDrag();
      event->accept();
      dragEnded();
    } else {
      QWidget::mouseReleaseEvent(event);
    }
  }

  void keyPressEvent(QKeyEvent *event) override
  {
    if (event->key() == Qt::Key_Escape) {
      event->accept();
      cancel();
    } else {
      QWidget::keyPressEvent(event);
    }
  }

  void closeEvent(QCloseEvent *event) override
  {
    event->accept();
    cancel();
  }

private:
  QString label() const
  {
    return m_point == Point::A ? QStringLiteral("A") : m_point == Point::B ? QStringLiteral("B") : QStringLiteral("C");
  }

  Point m_point;
  Edge m_edge = Edge::Right;
  bool m_pressed = false;
  QPointF m_pressPosition;
};

ScreenCalibrationOverlay::ScreenCalibrationOverlay(QWidget *owner) : QObject(owner), m_owner(owner)
{
  for (int i = 0; i < static_cast<int>(m_markers.size()); ++i) {
    auto *marker = new ScreenCalibrationMarker(static_cast<Point>(i), owner);
    marker->dragStarted = [this, i] { beginDrag(i); };
    marker->moved = [this, i](const QPointF &delta) { movePoint(i, delta); };
    marker->dragEnded = [this, i] { endDrag(i); };
    marker->cancel = [this] { requestCancel(); };
    m_markers[i] = marker;
  }
}

ScreenCalibrationOverlay::~ScreenCalibrationOverlay()
{
  const QSignalBlocker blocker(this);
  stop();
  for (auto *marker : m_markers)
    delete marker;
}

QColor ScreenCalibrationOverlay::pointColor(Point point)
{
  switch (point) {
  case Point::A:
    return QColor(0x19, 0x76, 0xd2);
  case Point::B:
    return QColor(0xf2, 0xa3, 0x3a);
  case Point::Check:
    return QColor(0x15, 0x85, 0x54);
  }
  return {};
}

QString ScreenCalibrationOverlay::unavailableReason()
{
  return unavailableReason(QGuiApplication::platformName(), QGuiApplication::screens().size());
}

QString ScreenCalibrationOverlay::unavailableReason(const QString &platformName, int screenCount)
{
  if (platformName.startsWith(QStringLiteral("wayland"), Qt::CaseInsensitive))
    return tr(
        "On-screen calibration is unavailable on Wayland because exact desktop-edge window placement is not supported. "
        "Use manual dimensions instead."
    );
  if (platformName != "windows" && platformName != "cocoa" && platformName != "xcb")
    return tr("This window system does not support on-screen calibration.");
  if (screenCount != 1)
    return tr(
        "On-screen calibration currently requires exactly one display on each computer. Use manual dimensions for "
        "other layouts."
    );
  return {};
}

QRect ScreenCalibrationOverlay::markerGeometryFor(Edge edge, const QRect &screen, double position)
{
  if (!validEdge(edge) || !screen.isValid() || !std::isfinite(position))
    return {};
  const QSize size = markerSize(edge);
  if (screen.width() < size.width() || screen.height() < size.height())
    return {};
  position = std::clamp(position, 0.0, 1.0);
  const auto tip = markerTip(edge);
  if (sideEdge(edge)) {
    // Round locally before adding the virtual-desktop origin: qRound's half
    // ties change sign around zero and otherwise shift translated markers.
    const int y =
        screen.y() + std::clamp(qRound(position * screen.height() - tip.y()), 0, screen.height() - size.height());
    const int x = edge == Edge::Left ? screen.x() : screen.x() + screen.width() - size.width();
    return {QPoint(x, y), size};
  }
  const int x = screen.x() + std::clamp(qRound(position * screen.width() - tip.x()), 0, screen.width() - size.width());
  const int y = edge == Edge::Top ? screen.y() : screen.y() + screen.height() - size.height();
  return {QPoint(x, y), size};
}

double ScreenCalibrationOverlay::normalizedPositionFor(Edge edge, const QRect &screen, const QRect &marker)
{
  if (!validEdge(edge) || !screen.isValid() || !marker.isValid())
    return std::numeric_limits<double>::quiet_NaN();
  const auto anchor = QPointF(marker.topLeft()) + markerTip(edge);
  return sideEdge(edge) ? (anchor.y() - screen.y()) / screen.height() : (anchor.x() - screen.x()) / screen.width();
}

QString ScreenCalibrationOverlay::showForEdge(Edge edge, const QList<double> &positions)
{
  if (m_running)
    return tr("Screen calibration is already active.");
  if (const auto reason = unavailableReason(); !reason.isEmpty())
    return reason;
  if (!validEdge(edge) || !validPositions(positions))
    return tr("The screen-edge marker positions are invalid.");
  if (auto *modal = QApplication::activeModalWidget();
      modal && (!m_owner || (modal != m_owner.data() && !modal->isAncestorOf(m_owner.data()))))
    return tr("Close the other dialog on this computer before starting screen calibration.");
  m_screen = QGuiApplication::screens().first();
  m_screenGeometry = m_screen->geometry();
  for (const auto position : positions) {
    if (markerGeometryFor(edge, m_screenGeometry, position).isEmpty())
      return tr("The display is too small to place calibration markers.");
  }
  m_edge = edge;
  m_positions = positions;
  m_running = true;
  ++m_generation;
  const auto changed = [this] {
    invalidate(tr("The display layout or scaling changed. Start screen calibration again."));
  };
  m_screenConnections = {
      connect(qGuiApp, &QGuiApplication::screenAdded, this, changed),
      connect(qGuiApp, &QGuiApplication::screenRemoved, this, changed),
      connect(m_screen, &QScreen::geometryChanged, this, changed),
      connect(m_screen, &QScreen::logicalDotsPerInchChanged, this, changed),
      connect(m_screen, &QScreen::physicalDotsPerInchChanged, this, changed),
      connect(m_screen, &QScreen::orientationChanged, this, changed),
  };
  qApp->installEventFilter(this);
  if (!placeMarkers())
    return tr("The calibration markers could not be displayed. Use manual dimensions instead.");
  const auto generation = m_generation;
  QTimer::singleShot(100, this, [this, generation] { verifyPlacement(generation); });
  return {};
}

void ScreenCalibrationOverlay::setPositions(const QList<double> &positions)
{
  if (!m_running)
    return;
  if (!validPositions(positions)) {
    invalidate(tr("The screen-edge marker positions are invalid."));
    return;
  }
  auto updated = positions;
  if (m_dragPoint >= updated.size()) {
    m_markers[m_dragPoint]->stopDrag();
    endDrag(m_dragPoint);
  }
  if (m_dragPoint >= 0 && m_dragPoint < updated.size())
    updated[m_dragPoint] = m_positions[m_dragPoint];
  m_positions = updated;
  placeMarkers();
}

bool ScreenCalibrationOverlay::placeMarkers()
{
  m_requestedGeometry.clear();
  for (int i = 0; i < static_cast<int>(m_markers.size()); ++i) {
    auto *marker = m_markers[i];
    if (i >= m_positions.size()) {
      marker->hide();
      continue;
    }
    marker->setEdge(m_edge);
    const auto requested = markerGeometryFor(m_edge, m_screenGeometry, m_positions[i]);
    marker->setGeometry(requested);
    m_requestedGeometry.append(requested);
    // Report the arrow's actual pixel-centered tip after rounding/clamping,
    // never the unclamped requested endpoint. No global-coordinate DPR math.
    m_positions[i] = normalizedPositionFor(m_edge, m_screenGeometry, marker->geometry());
    if (!marker->isVisible()) {
      marker->show();
      marker->raise();
#if defined(Q_OS_MACOS)
      if (!raiseOSXScreenCalibrationMarker(marker)) {
        invalidate(tr("The calibration markers could not be displayed. Use manual dimensions instead."));
        return false;
      }
#endif
    }
  }
  return true;
}

void ScreenCalibrationOverlay::verifyPlacement(quint64 generation)
{
  if (!m_running || m_generation != generation)
    return;
  for (int i = 0; i < m_positions.size(); ++i) {
    if (m_markers[i]->geometry() != m_requestedGeometry[i]) {
      invalidate(tr("The window manager moved a calibration marker away from the requested screen edge."));
      return;
    }
  }
}

void ScreenCalibrationOverlay::beginDrag(int point)
{
  if (!m_running || point >= m_positions.size())
    return;
  m_dragPoint = point;
  m_startPosition = m_positions[point];
  Q_EMIT dragStateChanged(true);
}

void ScreenCalibrationOverlay::movePoint(int point, const QPointF &delta)
{
  if (!m_running || m_dragPoint != point || point >= m_positions.size())
    return;
  const double displacement = sideEdge(m_edge) ? delta.y() : delta.x();
  const double extent = sideEdge(m_edge) ? m_screenGeometry.height() : m_screenGeometry.width();
  const double previous = m_positions[point];
  m_positions[point] = std::clamp(m_startPosition + displacement / extent, 0.0, 1.0);
  if (!placeMarkers())
    return;
  if (m_positions[point] != previous)
    Q_EMIT pointMoved(point, m_positions[point]);
}

void ScreenCalibrationOverlay::endDrag(int point)
{
  if (m_dragPoint != point)
    return;
  m_dragPoint = -1;
  Q_EMIT dragStateChanged(false);
}

void ScreenCalibrationOverlay::requestCancel()
{
  if (!m_running)
    return;
  stop();
  Q_EMIT cancelRequested();
}

void ScreenCalibrationOverlay::invalidate(const QString &reason)
{
  if (!m_running)
    return;
  stop();
  Q_EMIT invalidated(reason);
}

void ScreenCalibrationOverlay::stop()
{
  const bool dragged = m_dragPoint >= 0;
  m_running = false;
  m_dragPoint = -1;
  ++m_generation;
  if (qApp)
    qApp->removeEventFilter(this);
  for (const auto &connection : std::as_const(m_screenConnections))
    disconnect(connection);
  m_screenConnections.clear();
  for (auto *marker : m_markers) {
    marker->stopDrag();
    marker->hide();
  }
  if (dragged)
    Q_EMIT dragStateChanged(false);
}

QList<double> ScreenCalibrationOverlay::positions() const
{
  return m_positions;
}

bool ScreenCalibrationOverlay::isRunning() const
{
  return m_running;
}

QRect ScreenCalibrationOverlay::screenGeometry() const
{
  return m_screenGeometry;
}

double ScreenCalibrationOverlay::aspectRatio() const
{
  return m_screenGeometry.height() > 0 ? static_cast<double>(m_screenGeometry.width()) / m_screenGeometry.height() : 0;
}

QWidget *ScreenCalibrationOverlay::pointWidget(int point) const
{
  return point >= 0 && point < static_cast<int>(m_markers.size()) ? m_markers[point] : nullptr;
}

bool ScreenCalibrationOverlay::eventFilter(QObject *watched, QEvent *event)
{
  if (m_running && event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
    requestCancel();
    return true;
  }
  return QObject::eventFilter(watched, event);
}
