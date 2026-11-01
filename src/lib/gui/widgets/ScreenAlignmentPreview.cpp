/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenAlignmentPreview.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr double screenGap = 12;
constexpr double maximumOffset = 100000;

QColor blend(const QColor &base, const QColor &accent, double amount)
{
  return QColor::fromRgbF(
      base.redF() * (1 - amount) + accent.redF() * amount, base.greenF() * (1 - amount) + accent.greenF() * amount,
      base.blueF() * (1 - amount) + accent.blueF() * amount
  );
}
} // namespace

ScreenAlignmentPreview::ScreenAlignmentPreview(QWidget *parent) : QWidget(parent)
{
  setObjectName("alignmentPreview");
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setAccessibleName(tr("Screen alignment preview"));
}

QSize ScreenAlignmentPreview::sizeHint() const
{
  return {680, 240};
}

QSize ScreenAlignmentPreview::minimumSizeHint() const
{
  return {320, 180};
}

QRectF ScreenAlignmentPreview::firstScreenRect() const
{
  return m_firstRect;
}

QRectF ScreenAlignmentPreview::secondScreenRect() const
{
  return m_secondRect;
}

double ScreenAlignmentPreview::pixelsPerMm() const
{
  return m_scale;
}

void ScreenAlignmentPreview::setAlignment(const ScreenAlignment &alignment)
{
  const bool newPair = m_alignment.pair() != alignment.pair();
  if (newPair || !alignment.enabled)
    finishDrag();
  m_alignment = alignment;
  setToolTip(
      alignment.orientation == Qt::Horizontal
          ? tr("Drag the second screen up or down. Arrow keys move it by 1 mm; Shift moves it by 10 mm.")
          : tr("Drag the second screen left or right. Arrow keys move it by 1 mm; Shift moves it by 10 mm.")
  );
  // Freeze both scale and origin throughout a drag. Otherwise changing the
  // offset would re-fit the scene under the pointer and cause oscillation.
  if (!m_dragging)
    fitScreens();
  else
    updateScreenRects();
  update();
}

void ScreenAlignmentPreview::fitScreens()
{
  if (!m_alignment.isValid() || !m_alignment.intervals()) {
    m_firstRect = {};
    m_secondRect = {};
    return;
  }
  const bool horizontal = m_alignment.orientation == Qt::Horizontal;
  const QRectF first(QPointF(), m_alignment.firstSizeMm);
  const QRectF second(
      horizontal ? QPointF(first.width(), m_alignment.offsetMm) : QPointF(m_alignment.offsetMm, first.height()),
      m_alignment.secondSizeMm
  );
  const QRectF bounds = first.united(second);
  if (!std::isfinite(bounds.x()) || !std::isfinite(bounds.y()) || !std::isfinite(bounds.width()) ||
      !std::isfinite(bounds.height()) || bounds.width() <= 0 || bounds.height() <= 0) {
    m_firstRect = {};
    m_secondRect = {};
    return;
  }
  const QRectF available = rect().adjusted(20, 18, -20, -18);
  if (available.width() <= screenGap || available.height() <= screenGap)
    return;
  m_scale = std::min(
      (available.width() - (horizontal ? screenGap : 0)) / bounds.width(),
      (available.height() - (horizontal ? 0 : screenGap)) / bounds.height()
  );
  if (!std::isfinite(m_scale) || m_scale <= 0) {
    m_firstRect = {};
    m_secondRect = {};
    return;
  }
  const QSizeF fitted(
      bounds.width() * m_scale + (horizontal ? screenGap : 0), bounds.height() * m_scale + (horizontal ? 0 : screenGap)
  );
  m_origin = available.center() - QPointF(fitted.width() / 2, fitted.height() / 2) - bounds.topLeft() * m_scale;
  updateScreenRects();
}

void ScreenAlignmentPreview::updateScreenRects()
{
  m_firstRect = QRectF(m_origin, m_alignment.firstSizeMm * m_scale);
  const bool horizontal = m_alignment.orientation == Qt::Horizontal;
  const QPointF secondPosition =
      horizontal ? QPointF(m_firstRect.right() + screenGap, m_origin.y() + m_alignment.offsetMm * m_scale)
                 : QPointF(m_origin.x() + m_alignment.offsetMm * m_scale, m_firstRect.bottom() + screenGap);
  m_secondRect = QRectF(secondPosition, m_alignment.secondSizeMm * m_scale);
}

void ScreenAlignmentPreview::paintEvent(QPaintEvent *)
{
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(rect(), palette().color(QPalette::Base));
  painter.setPen(QPen(palette().color(QPalette::Mid), 1));
  painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
  if (m_firstRect.isEmpty() || m_secondRect.isEmpty()) {
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(rect(), Qt::AlignCenter, tr("Enter valid screen dimensions to preview the alignment."));
    return;
  }

  const QColor accent = palette().color(QPalette::Highlight);
  const auto drawScreen = [&](const QRectF &screen, const QString &name, const QSizeF &size, bool second) {
    painter.setBrush(blend(palette().color(QPalette::Base), accent, second ? 0.16 : 0.07));
    painter.setPen(QPen(second ? accent : palette().color(QPalette::Mid), second ? 2 : 1.5));
    painter.drawRoundedRect(screen, 6, 6);
    painter.setPen(palette().color(QPalette::Text));
    QFont title = font();
    title.setBold(true);
    painter.setFont(title);
    const QRectF label = screen.adjusted(10, 8, -10, -8);
    if (label.width() > 40 && label.height() > 42) {
      const auto text = QFontMetrics(title).elidedText(name, Qt::ElideMiddle, qMax(1, qRound(label.width())));
      painter.drawText(label.adjusted(0, 0, 0, -20), Qt::AlignCenter, text);
      painter.setFont(font());
      const auto dimensions = tr("%1 × %2 mm").arg(size.width(), 0, 'f', 1).arg(size.height(), 0, 'f', 1);
      painter.drawText(
          label.adjusted(0, 22, 0, 0), Qt::AlignCenter,
          QFontMetrics(font()).elidedText(dimensions, Qt::ElideRight, qMax(1, qRound(label.width())))
      );
    } else {
      painter.drawText(screen, Qt::AlignCenter, second ? QStringLiteral("2") : QStringLiteral("1"));
    }
  };
  drawScreen(m_firstRect, m_alignment.firstName, m_alignment.firstSizeMm, false);
  drawScreen(m_secondRect, m_alignment.secondName, m_alignment.secondSizeMm, true);

  const auto intervals = m_alignment.intervals();
  if (m_alignment.enabled && intervals && intervals->hasOverlap) {
    const bool horizontal = m_alignment.orientation == Qt::Horizontal;
    const QColor green = palette().color(QPalette::Base).lightness() < 128 ? QColor(88, 211, 142) : QColor(21, 137, 75);
    const auto edge = [horizontal](const QRectF &screen, const ScreenAlignmentRange &range, bool second) {
      if (horizontal) {
        const double x = second ? screen.left() : screen.right();
        return QLineF(
            x, screen.top() + screen.height() * range.startPercent / 100, x,
            screen.top() + screen.height() * range.endPercent / 100
        );
      }
      const double y = second ? screen.top() : screen.bottom();
      return QLineF(
          screen.left() + screen.width() * range.startPercent / 100, y,
          screen.left() + screen.width() * range.endPercent / 100, y
      );
    };
    const auto firstEdge = edge(m_firstRect, intervals->first, false);
    const auto secondEdge = edge(m_secondRect, intervals->second, true);
    QPainterPath bridge;
    bridge.moveTo(firstEdge.p1());
    bridge.lineTo(firstEdge.p2());
    bridge.lineTo(secondEdge.p2());
    bridge.lineTo(secondEdge.p1());
    bridge.closeSubpath();
    QColor fill = green;
    fill.setAlpha(55);
    painter.fillPath(bridge, fill);
    painter.setPen(QPen(green, 4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(firstEdge);
    painter.drawLine(secondEdge);
  }

  if (hasFocus() && m_alignment.enabled) {
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(accent, 1, Qt::DashLine));
    painter.drawRoundedRect(m_secondRect.adjusted(-4, -4, 4, 4), 8, 8);
  }
}

void ScreenAlignmentPreview::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);
  if (!m_dragging)
    fitScreens();
}

void ScreenAlignmentPreview::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton && m_alignment.enabled && m_secondRect.contains(event->position())) {
    setFocus(Qt::MouseFocusReason);
    m_dragging = true;
    m_dragStart = event->position();
    m_startOffset = m_alignment.offsetMm;
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void ScreenAlignmentPreview::mouseMoveEvent(QMouseEvent *event)
{
  if (m_dragging) {
    const auto delta = event->position() - m_dragStart;
    const double displacement = m_alignment.orientation == Qt::Horizontal ? delta.y() : delta.x();
    const double offset = std::clamp(m_startOffset + displacement / m_scale, -maximumOffset, maximumOffset);
    Q_EMIT offsetEdited(std::round(offset * 10) / 10);
    event->accept();
    return;
  }
  if (m_alignment.enabled && m_secondRect.contains(event->position()))
    setCursor(m_alignment.orientation == Qt::Horizontal ? Qt::SizeVerCursor : Qt::SizeHorCursor);
  else
    unsetCursor();
  QWidget::mouseMoveEvent(event);
}

void ScreenAlignmentPreview::mouseReleaseEvent(QMouseEvent *event)
{
  if (event->button() == Qt::LeftButton && m_dragging) {
    finishDrag();
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void ScreenAlignmentPreview::finishDrag()
{
  if (!m_dragging)
    return;
  m_dragging = false;
  fitScreens();
  update();
}

void ScreenAlignmentPreview::keyPressEvent(QKeyEvent *event)
{
  if (m_alignment.enabled) {
    const bool horizontal = m_alignment.orientation == Qt::Horizontal;
    const int negative = horizontal ? Qt::Key_Up : Qt::Key_Left;
    const int positive = horizontal ? Qt::Key_Down : Qt::Key_Right;
    if (event->key() == negative || event->key() == positive) {
      const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
      Q_EMIT offsetEdited(
          std::clamp(m_alignment.offsetMm + (event->key() == negative ? -step : step), -maximumOffset, maximumOffset)
      );
      event->accept();
      return;
    }
  }
  QWidget::keyPressEvent(event);
}

void ScreenAlignmentPreview::changeEvent(QEvent *event)
{
  if (event->type() == QEvent::EnabledChange && !isEnabled())
    finishDrag();
  QWidget::changeEvent(event);
}
