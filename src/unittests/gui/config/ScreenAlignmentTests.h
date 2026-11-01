/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class ScreenAlignmentTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void init();
  void geometry_data();
  void geometry();
  void noOverlap_data();
  void noOverlap();
  void invalidGeometry();
  void defaultLinksAndCopy();
  void reciprocalExport();
  void verticalExport();
  void disjointExport();
  void fractionalPersistenceAndLocale();
  void topologyChanges_data();
  void topologyChanges();
  void movedPairKeepsCalibration();
  void serverRenameClearsCalibration();
  void malformedMetadataIgnored();
  void calibrationTransform_data();
  void calibrationTransform();
  void calibrationValidation();
  void calibrationThirdPoint();
  void relativeGeometryAndIntervals();
  void calibrationPersistence();
  void malformedCalibrationMetadata_data();
  void malformedCalibrationMetadata();
  void manualReplacesCalibration();
};
