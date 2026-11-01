/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class ScreenCalibrationTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void unavailableStart();
  void readyMappingAndCancellation();
  void invalidGeometry_data();
  void invalidGeometry();
  void staleMessagesAndRevisions();
  void checkPointRequiresThreePositions();
  void checkPointDoesNotRefit();
  void geometryChangesAbort();
  void timeoutErrorAndRetry();
  void wizardAcceptAndCancel();
  void wizardFailureAndRetry();
  void failedWizardCannotCancelAnotherSession();
  void compactFeedbackStaysVisible();
  void renderExamples();
};
