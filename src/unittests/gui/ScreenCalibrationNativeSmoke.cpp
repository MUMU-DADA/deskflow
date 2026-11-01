/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/widgets/ScreenCalibrationOverlay.h"

#include <QApplication>
#include <QDebug>
#include <QDialog>
#include <QEventLoop>
#include <QLabel>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

namespace {
int runPass(bool modal)
{
  using Overlay = ScreenCalibrationOverlay;
  const QList<Overlay::Edge> edges{
      Overlay::Edge::Left, Overlay::Edge::Right, Overlay::Edge::Top, Overlay::Edge::Bottom
  };
  const QList<double> requested{0.25, 0.75};
  int result = 1;
  int nextEdge = 0;
  QPointer<Overlay> overlay;
  QDialog modalOwner;
  QWidget hiddenOwner;
  QWidget *owner = modal ? &modalOwner : &hiddenOwner;
  QEventLoop nonModalLoop;
  owner->setWindowTitle("Deskflow native marker smoke test");
  auto *layout = new QVBoxLayout(owner);
  layout->addWidget(new QLabel("Temporary test windows close automatically.", owner));
  owner->resize(420, 90);
  const auto finish = [&](bool success) {
    result = success ? 0 : 1;
    if (overlay)
      overlay->stop();
    if (modal)
      modalOwner.accept();
    else
      nonModalLoop.quit();
  };
  const auto accurate = [&] {
    if (!overlay || !overlay->isRunning() || !overlay->isPlacementReady())
      return false;
    if ((QApplication::activeModalWidget() == owner) != modal || (!modal && owner->isVisible())) {
      qWarning() << "Unexpected native smoke owner state" << "modal pass:" << modal;
      return false;
    }
    const auto edge = edges[nextEdge - 1];
    for (int i = 0; i < requested.size(); ++i) {
      const auto expected = Overlay::markerGeometryFor(edge, overlay->screenGeometry(), requested[i]);
      auto *marker = overlay->pointWidget(i);
      const auto actualPosition = Overlay::normalizedPositionFor(edge, overlay->screenGeometry(), marker->geometry());
      if (!marker->isVisible() || marker->geometry() != expected || overlay->positions()[i] != actualPosition) {
        qWarning() << "Native marker mismatch" << static_cast<int>(edge) << i << "requested" << expected << "actual"
                   << marker->geometry() << "frame" << marker->frameGeometry();
        return false;
      }
    }
    return true;
  };
  std::function<void()> startNext;
  startNext = [&] {
    if (overlay) {
      overlay->stop();
      delete overlay.data();
    }
    if (nextEdge == edges.size()) {
      finish(true);
      return;
    }
    overlay = new Overlay(owner);
    QObject::connect(overlay, &Overlay::invalidated, owner, [&](const QString &reason) {
      qWarning().noquote() << "Native calibration invalidated:" << overlay->failureCode() << reason;
      finish(false);
    });
    QObject::connect(overlay, &Overlay::placementReady, owner, [&] {
      if (!accurate()) {
        finish(false);
        return;
      }
      QTimer::singleShot(150, owner, [&] {
        if (!accurate()) {
          finish(false);
          return;
        }
        startNext();
      });
    });
    const auto problem = overlay->showForEdge(edges[nextEdge++], requested);
    if (!problem.isEmpty()) {
      qWarning().noquote() << "Native calibration show failed:" << overlay->failureCode() << problem;
      finish(false);
    }
  };
  QTimer::singleShot(0, owner, startNext);
  QTimer::singleShot(5000, owner, [&] {
    qWarning("Native calibration smoke timed out");
    finish(false);
  });
  if (modal)
    modalOwner.exec();
  else
    nonModalLoop.exec(); // The remote GUI's ordinary owner deliberately stays hidden.
  if (overlay)
    overlay->stop();
  qInfo() << "Native calibration smoke result:" << result << "modal owner:" << modal << "edges:" << nextEdge;
  return result;
}
} // namespace

// Dedicated macOS CI smoke executable. It creates only its own short-lived
// Cocoa windows; no Settings, core, IPC, network, or input injection is involved.
// Offscreen tests cannot validate AppKit's first-show frame/style conversion.
int main(int argc, char **argv)
{
#if !defined(Q_OS_MACOS)
  Q_UNUSED(argc)
  Q_UNUSED(argv)
  return 77;
#else
  qputenv("QT_QPA_PLATFORM", "cocoa");
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  if (QGuiApplication::platformName() != "cocoa" || QGuiApplication::screens().size() != 1) {
    qWarning("Native calibration smoke requires a Cocoa session with one screen");
    return 77;
  }
  if (const int result = runPass(true); result != 0)
    return result;
  return runPass(false);
#endif
}
