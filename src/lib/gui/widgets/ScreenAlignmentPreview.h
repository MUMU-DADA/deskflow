/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "gui/config/ScreenAlignment.h"

#include <QPointF>
#include <QRectF>
#include <QWidget>

class ScreenAlignmentPreview : public QWidget
{
  Q_OBJECT

public:
  explicit ScreenAlignmentPreview(QWidget *parent = nullptr);
  void setAlignment(const ScreenAlignment &alignment);
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;
  QRectF firstScreenRect() const;
  QRectF secondScreenRect() const;
  double pixelsPerMm() const;

Q_SIGNALS:
  void offsetEdited(double offsetMm);

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  void fitScreens();
  void updateScreenRects();
  void finishDrag();

  ScreenAlignment m_alignment;
  std::optional<ScreenAlignmentVisualGeometry> m_visual;
  QPointF m_origin;
  QRectF m_firstRect;
  QRectF m_secondRect;
  double m_scale = 1;
  bool m_dragging = false;
  QPointF m_dragStart;
  double m_startOffset = 0;
};
