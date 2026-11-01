/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "gui/config/ScreenAlignment.h"

#include <QDialog>
#include <QPointer>

class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QPushButton;
class ScreenAlignmentPreview;
class ScreenCalibrationController;

class ScreenCalibrationDialog : public QDialog
{
  Q_OBJECT

public:
  ScreenCalibrationDialog(QWidget *parent, ScreenCalibrationController &controller, const ScreenAlignment &alignment);
  ~ScreenCalibrationDialog() override;

  // A snapshot is available only after Use calibration succeeds. The caller
  // owns the staged configuration and decides when to save it.
  std::optional<ScreenEdgeCalibration> calibration() const;

public Q_SLOTS:
  void accept() override;
  void reject() override;

private:
  void startSession();
  void updateState();
  void sessionCancelled(const QString &reason);
  void stopSession();

  QPointer<ScreenCalibrationController> m_controller;
  ScreenAlignment m_alignment;
  std::optional<ScreenEdgeCalibration> m_calibration;
  QString m_failureReason;
  bool m_failed = false;
  bool m_started = false;
  bool m_finished = false;
  bool m_hasPreview = false;
  bool m_ownsSession = false;

  ScreenAlignmentPreview *m_preview = nullptr;
  QLabel *m_previewHint = nullptr;
  QLabel *m_firstState = nullptr;
  QLabel *m_secondState = nullptr;
  QLabel *m_measurement = nullptr;
  QLabel *m_status = nullptr;
  QCheckBox *m_checkPoint = nullptr;
  QDialogButtonBox *m_buttons = nullptr;
  QPushButton *m_use = nullptr;
  QPushButton *m_retry = nullptr;
};
