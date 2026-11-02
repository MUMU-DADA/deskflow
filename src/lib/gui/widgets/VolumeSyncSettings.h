/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "common/VolumeSync.h"
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;
class ServerConfig;

class VolumeSyncSettings : public QWidget
{
  Q_OBJECT
public:
  VolumeSyncSettings(
      const ServerConfig &config, QWidget *parent = nullptr, std::unique_ptr<deskflow::volume::Control> control = {}
  );
  void load();
  void save();
  void resetToDefault();
  void updateControls();
  void retranslate();
  bool isModified() const;
  bool isDefault() const;
  deskflow::volume::Weights weights() const;

Q_SIGNALS:
  void changed();

private:
  void addRow(const QString &name, int reference);
  void addComputer();
  void updateDevice();
  bool m_serverMode;
  QStringList m_computers;
  std::unique_ptr<deskflow::volume::Control> m_control;
  QCheckBox *m_enable;
  QLabel *m_help;
  QLabel *m_device;
  QTableWidget *m_table;
  QPushButton *m_add;
  QPushButton *m_remove;
};
