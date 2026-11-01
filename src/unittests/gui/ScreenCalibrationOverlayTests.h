/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class ScreenCalibrationOverlayTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void geometry_data();
  void geometry();
  void edgeEndpointsStayVisible();
  void logicalExtentsDoNotDependOnDpi();
  void unsupportedPlatformsAndMultipleScreens();
  void ownerDestructionDeletesMarkerWindows();
  void independentOverlayDestructionIsSafe();
  void relativePreviewIsReadOnly();
  void renderExamples();
};
