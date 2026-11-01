/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenCalibrationOverlayTests.h"

#include "gui/widgets/ScreenAlignmentPreview.h"
#include "gui/widgets/ScreenCalibrationOverlay.h"

#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QScopeGuard>
#include <QSignalSpy>

#include <cmath>
#include <limits>

using Overlay = ScreenCalibrationOverlay;
using Edge = Overlay::Edge;

void ScreenCalibrationOverlayTests::geometry_data()
{
  QTest::addColumn<int>("edgeValue");
  QTest::addColumn<QRect>("screen");
  for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) {
    QTest::newRow(qPrintable(QString("negative-origin-%1").arg(static_cast<int>(edge))))
        << static_cast<int>(edge) << QRect(-1920, -240, 1920, 1080);
    QTest::newRow(qPrintable(QString("offset-origin-%1").arg(static_cast<int>(edge))))
        << static_cast<int>(edge) << QRect(123, 75, 900, 1600);
  }
}

void ScreenCalibrationOverlayTests::geometry()
{
  QFETCH(int, edgeValue);
  QFETCH(QRect, screen);
  const auto edge = static_cast<Edge>(edgeValue);
  const bool side = edge == Edge::Left || edge == Edge::Right;
  const double extent = side ? screen.height() : screen.width();
  for (double requested : {0.2, 0.35, 0.8}) {
    const QRect marker = Overlay::markerGeometryFor(edge, screen, requested);
    QVERIFY(screen.contains(marker));
    switch (edge) {
    case Edge::Left:
      QCOMPARE(marker.left(), screen.left());
      break;
    case Edge::Right:
      QCOMPARE(marker.right(), screen.right());
      break;
    case Edge::Top:
      QCOMPARE(marker.top(), screen.top());
      break;
    case Edge::Bottom:
      QCOMPARE(marker.bottom(), screen.bottom());
      break;
    }
    const double actual = Overlay::normalizedPositionFor(edge, screen, marker);
    QVERIFY(std::abs(actual - requested) <= 0.5 / extent + 1e-12);
    // The rendered arrow tip lies at the center of the marker's short axis.
    // The denominator is the complete edge extent, not extent-1/work area.
    const auto center = QRectF(marker).center();
    QCOMPARE(actual, side ? (center.y() - screen.y()) / screen.height() : (center.x() - screen.x()) / screen.width());
    QCOMPARE(Overlay::markerGeometryFor(edge, screen, actual), marker);
    const QPoint translation(-311, 149);
    QCOMPARE(
        Overlay::markerGeometryFor(edge, screen.translated(translation), requested), marker.translated(translation)
    );
  }
}

void ScreenCalibrationOverlayTests::edgeEndpointsStayVisible()
{
  const QRect screen(-123, 41, 1600, 900);
  for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) {
    const auto start = Overlay::markerGeometryFor(edge, screen, 0);
    const auto end = Overlay::markerGeometryFor(edge, screen, 1);
    QVERIFY(screen.contains(start));
    QVERIFY(screen.contains(end));
    const double startPosition = Overlay::normalizedPositionFor(edge, screen, start);
    const double endPosition = Overlay::normalizedPositionFor(edge, screen, end);
    QVERIFY(startPosition > 0 && startPosition < 0.1);
    QVERIFY(endPosition > 0.9 && endPosition < 1);
    QCOMPARE(Overlay::markerGeometryFor(edge, screen, startPosition), start);
    QCOMPARE(Overlay::markerGeometryFor(edge, screen, endPosition), end);
  }
  QVERIFY(Overlay::markerGeometryFor(Edge::Right, QRect(0, 0, 20, 20), 0.5).isEmpty());
  QVERIFY(Overlay::markerGeometryFor(Edge::Right, screen, std::numeric_limits<double>::quiet_NaN()).isEmpty());
}

void ScreenCalibrationOverlayTests::logicalExtentsDoNotDependOnDpi()
{
  // The same native 2560x1440 output could expose these Qt logical extents.
  // Normalization stays local to each logical screen; there is no DPR input.
  for (const QSize size : {QSize(2560, 1440), QSize(1707, 960), QSize(1280, 720)}) {
    const QRect screen(QPoint(-900, 123), size);
    const auto marker = Overlay::markerGeometryFor(Edge::Right, screen, 0.7);
    const double actual = Overlay::normalizedPositionFor(Edge::Right, screen, marker);
    QVERIFY(std::abs(actual - 0.7) <= 0.5 / size.height() + 1e-12);
    QCOMPARE(marker.size(), QSize(69, 37)); // Marker/grip size is also in logical pixels.
  }
}

void ScreenCalibrationOverlayTests::unsupportedPlatformsAndMultipleScreens()
{
  for (const auto &platform : {QStringLiteral("windows"), QStringLiteral("cocoa"), QStringLiteral("xcb")}) {
    QVERIFY(Overlay::unavailableReason(platform, 1).isEmpty());
    QVERIFY(!Overlay::unavailableReason(platform, 0).isEmpty());
    QVERIFY(!Overlay::unavailableReason(platform, 2).isEmpty());
  }
  QVERIFY(Overlay::unavailableReason("wayland", 1).contains("Wayland"));
  QVERIFY(Overlay::unavailableReason("wayland-egl", 1).contains("Wayland"));
  QVERIFY(!Overlay::unavailableReason("offscreen", 1).isEmpty());
  Overlay overlay;
  QVERIFY(!overlay.showForEdge(Edge::Right, {0.2, 0.8}).isEmpty());
  QVERIFY(!overlay.isRunning());
  for (int i = 0; i < 3; ++i)
    QVERIFY(!overlay.pointWidget(i)->isVisible());
}

void ScreenCalibrationOverlayTests::ownerDestructionDeletesMarkerWindows()
{
  auto *owner = new QWidget;
  QPointer<Overlay> overlay = new Overlay(owner);
  QPointer<QWidget> marker = overlay->pointWidget(0);
  QCOMPARE(marker->parentWidget(), owner);
  QVERIFY(marker->windowFlags().testFlag(Qt::Tool));
  QVERIFY(marker->windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
  QVERIFY(marker->testAttribute(Qt::WA_ShowWithoutActivating));
  delete owner;
  QVERIFY(overlay.isNull());
  QVERIFY(marker.isNull());
}

void ScreenCalibrationOverlayTests::independentOverlayDestructionIsSafe()
{
  QWidget owner;
  auto *overlay = new Overlay(&owner);
  QPointer<QWidget> marker = overlay->pointWidget(1);
  overlay->stop();
  overlay->stop();
  delete overlay;
  QVERIFY(marker.isNull());
  QVERIFY(owner.children().isEmpty());
}

void ScreenCalibrationOverlayTests::relativePreviewIsReadOnly()
{
  ScreenAlignment alignment{"Windows", "Mac", Qt::Horizontal, QSizeF(9999, 8888), QSizeF(7777, 6666), 5555, true};
  ScreenEdgeCalibration calibration;
  calibration.a = {0.2, 0.25};
  calibration.b = {0.8, 0.75};
  calibration.firstAspectRatio = 16.0 / 9.0;
  calibration.secondAspectRatio = 9.0 / 16.0;
  alignment.calibration = calibration;
  ScreenAlignmentPreview preview;
  preview.resize(640, 280);
  preview.setAlignment(alignment);
  preview.show(); // This test's main always selects the offscreen plugin.
  QCoreApplication::processEvents();
  QCOMPARE(preview.pixelsPerMm(), 0);
  QVERIFY(preview.toolTip().contains("relative proportions"));
  QVERIFY(!preview.toolTip().contains("1 mm"));
  const auto first = preview.firstScreenRect();
  const auto second = preview.secondScreenRect();
  QVERIFY(std::abs(first.width() / first.height() - 16.0 / 9.0) < 1e-10);
  QVERIFY(std::abs(second.width() / second.height() - 9.0 / 16.0) < 1e-10);
  QVERIFY(std::abs(second.height() / first.height() - 1.2) < 1e-10);
  QSignalSpy edited(&preview, &ScreenAlignmentPreview::offsetEdited);
  const auto start = second.center();
  QTest::mousePress(&preview, Qt::LeftButton, Qt::NoModifier, start.toPoint());
  QMouseEvent move(
      QEvent::MouseMove, start + QPointF(0, 30), preview.mapToGlobal(start + QPointF(0, 30)), Qt::NoButton,
      Qt::LeftButton, Qt::NoModifier
  );
  QApplication::sendEvent(&preview, &move);
  QTest::mouseRelease(&preview, Qt::LeftButton, Qt::NoModifier, (start + QPointF(0, 30)).toPoint());
  QTest::keyClick(&preview, Qt::Key_Down);
  QCOMPARE(edited.count(), 0);
  QCOMPARE(preview.firstScreenRect(), first);
  QCOMPARE(preview.secondScreenRect(), second);
}

void ScreenCalibrationOverlayTests::renderExamples()
{
  const auto directory = qEnvironmentVariable("DESKFLOW_OVERLAY_RENDER_DIR");
  if (directory.isEmpty())
    return;
  QVERIFY(QDir().mkpath(directory));
  const QPalette original = qApp->palette();
  const auto restore = qScopeGuard([&] { qApp->setPalette(original); });
  for (bool dark : {false, true}) {
    auto palette = original;
    palette.setColor(QPalette::Window, dark ? QColor(34, 38, 42) : QColor(248, 248, 248));
    palette.setColor(QPalette::Base, dark ? QColor(25, 29, 32) : Qt::white);
    palette.setColor(QPalette::Text, dark ? Qt::white : Qt::black);
    palette.setColor(QPalette::WindowText, dark ? Qt::white : Qt::black);
    qApp->setPalette(palette);
    const QString theme = dark ? "dark" : "light";
    Overlay overlay;
    QImage atlas(330, 90, QImage::Format_ARGB32_Premultiplied);
    atlas.fill(palette.color(QPalette::Window));
    QPainter painter(&atlas);
    painter.setPen(palette.color(QPalette::Text));
    for (int i = 0; i < 3; ++i) {
      auto *marker = overlay.pointWidget(i);
      QVERIFY(!marker->isVisible());
      const auto picture = marker->grab();
      QVERIFY(picture.save(QDir(directory).filePath(QString("%1-marker-%2.png").arg(theme).arg(i))));
      painter.drawPixmap(20 + i * 105, 20, picture);
      painter.drawText(QRect(12 + i * 105, 62, 90, 20), Qt::AlignCenter, QString(QChar('A' + i)));
      QVERIFY(!marker->isVisible());
    }
    painter.end();
    QVERIFY(atlas.save(QDir(directory).filePath(theme + "-markers.png")));

    ScreenAlignment alignment{"Windows", "Mac", Qt::Horizontal, {}, {}, 0, true};
    alignment.calibration = ScreenEdgeCalibration{{0.2, 0.3}, {0.8, 0.7}, 16.0 / 9.0, 9.0 / 16.0};
    ScreenAlignmentPreview preview;
    preview.resize(640, 280);
    preview.setAlignment(alignment);
    QVERIFY(preview.grab().save(QDir(directory).filePath(theme + "-relative-preview.png")));
  }
}

int main(int argc, char **argv)
{
  // Never display real desktop markers while running unit tests on a developer's machine.
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  app.setStyle("fusion");
  for (const auto &font : qEnvironmentVariable("DESKFLOW_ALIGNMENT_FONT_FILES").split(';', Qt::SkipEmptyParts)) {
    if (QFontDatabase::addApplicationFont(font) < 0)
      return 3;
  }
  const auto family = qEnvironmentVariable("DESKFLOW_ALIGNMENT_FONT_FAMILY");
  if (!family.isEmpty())
    app.setFont(QFont(family, 9));
  ScreenCalibrationOverlayTests tests;
  return QTest::qExec(&tests, argc, argv);
}
