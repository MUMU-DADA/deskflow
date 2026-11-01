/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QRect>

#include <array>

class QScreen;
class QWidget;
class ScreenCalibrationMarker;

// GUI-process only. Each marker is a small tool window, not a transparent
// screen-sized input surface. The controller owns session/hold leases and the
// global Escape cancellation route; this class handles only local Qt events.
class ScreenCalibrationOverlay : public QObject
{
  Q_OBJECT

public:
  enum class Point
  {
    A,
    B,
    Check
  };
  Q_ENUM(Point)
  enum class Edge
  {
    Left,
    Right,
    Top,
    Bottom
  };
  Q_ENUM(Edge)

  explicit ScreenCalibrationOverlay(QWidget *owner = nullptr);
  ~ScreenCalibrationOverlay() override;

  static QString unavailableReason();
  static QString unavailableReason(const QString &platformName, int screenCount);
  static QColor pointColor(Point point);
  // Pure geometry helpers also permit offscreen tests without claiming that
  // the offscreen/Wayland platform can place actual desktop-edge windows.
  static QRect markerGeometryFor(Edge edge, const QRect &screen, double position);
  static double normalizedPositionFor(Edge edge, const QRect &screen, const QRect &marker);

  // Empty string means success. Coordinates are fractions of the complete
  // screen edge, in top-to-bottom or left-to-right order, never work-area units.
  QString showForEdge(Edge edge, const QList<double> &positions);
  void setPositions(const QList<double> &positions);
  QList<double> positions() const;
  void stop();
  bool isRunning() const;
  QRect screenGeometry() const;
  double aspectRatio() const;
  QWidget *pointWidget(int point) const;

Q_SIGNALS:
  void pointMoved(int point, double position);
  void dragStateChanged(bool dragging);
  void cancelRequested();
  void invalidated(const QString &reason);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  bool placeMarkers();
  void verifyPlacement(quint64 generation);
  void beginDrag(int point);
  void movePoint(int point, const QPointF &delta);
  void endDrag(int point);
  void requestCancel();
  void invalidate(const QString &reason);

  QPointer<QWidget> m_owner;
  QPointer<QScreen> m_screen;
  std::array<ScreenCalibrationMarker *, 3> m_markers{};
  QList<QMetaObject::Connection> m_screenConnections;
  QList<double> m_positions;
  QList<QRect> m_requestedGeometry;
  QRect m_screenGeometry;
  Edge m_edge = Edge::Right;
  bool m_running = false;
  int m_dragPoint = -1;
  double m_startPosition = 0;
  quint64 m_generation = 0;
};
