/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QtTest>

class ScreenAlignmentDialogTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void init();
  void saveAndCancel();
  void overlapAndReset();
  void presetsAndPairSelection();
  void offsetEditPreservesStoredDimensions();
  void outerSaveAndCancel();
  void externalAndEmptyLayouts();
  void compactFeedbackStaysVisible();
  void dragUsesOneAxis_data();
  void dragUsesOneAxis();
  void renderExamples();
};
