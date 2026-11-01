/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "gui/config/ScreenAlignment.h"

#include <QDialog>
#include <QList>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class ScreenAlignmentPreview;
class ScreenCalibrationController;
class ServerConfig;

class ScreenAlignmentDialog : public QDialog
{
  Q_OBJECT

public:
  explicit ScreenAlignmentDialog(
      QWidget *parent, ServerConfig &config, const QString &preferredComputer = {},
      ScreenCalibrationController *calibrationController = nullptr
  );

public Q_SLOTS:
  void accept() override;

private:
  ScreenAlignment initialAlignment(const ScreenAlignmentPair &pair) const;
  void selectPair(int index);
  void controlsChanged();
  void updateFeedback();
  void resetAlignment();
  void alignScreens(int alignment);
  void swapDimensions(bool second);
  void calibrateEdges();
  void useManualDimensions();
  bool canSave() const;

  ServerConfig &m_config;
  ScreenCalibrationController *m_calibrationController = nullptr;
  QList<ScreenAlignment> m_drafts;
  QList<bool> m_unsupported;
  QList<QString> m_firstHints;
  QList<QString> m_secondHints;
  int m_current = -1;
  bool m_loading = false;

  QComboBox *m_pair = nullptr;
  QCheckBox *m_enabled = nullptr;
  QPushButton *m_calibrate = nullptr;
  QPushButton *m_manual = nullptr;
  QLabel *m_calibrationSummary = nullptr;
  ScreenAlignmentPreview *m_preview = nullptr;
  QWidget *m_geometry = nullptr;
  QGroupBox *m_firstGroup = nullptr;
  QGroupBox *m_secondGroup = nullptr;
  QDoubleSpinBox *m_firstWidth = nullptr;
  QDoubleSpinBox *m_firstHeight = nullptr;
  QDoubleSpinBox *m_secondWidth = nullptr;
  QDoubleSpinBox *m_secondHeight = nullptr;
  QDoubleSpinBox *m_offset = nullptr;
  QLabel *m_firstHint = nullptr;
  QLabel *m_secondHint = nullptr;
  QLabel *m_offsetLabel = nullptr;
  QLabel *m_offsetHelp = nullptr;
  QLabel *m_mapping = nullptr;
  QLabel *m_status = nullptr;
  QPushButton *m_alignStart = nullptr;
  QPushButton *m_alignEnd = nullptr;
  QPushButton *m_reset = nullptr;
  QDialogButtonBox *m_buttons = nullptr;
};
