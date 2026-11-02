/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "VolumeSyncSettings.h"
#include "common/Settings.h"
#include "gui/config/ServerConfig.h"
#include "platform/SystemVolume.h"
#include <QCheckBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
deskflow::volume::Weights normalized(deskflow::volume::Weights weights)
{
  for (auto i = weights.begin(); i != weights.end();) {
    if (i.value() == 100)
      i = weights.erase(i);
    else
      ++i;
  }
  return weights;
}
} // namespace

VolumeSyncSettings::VolumeSyncSettings(
    const ServerConfig &config, QWidget *parent, std::unique_ptr<deskflow::volume::Control> control
)
    : QWidget(parent),
      m_serverMode(Settings::value(Settings::Core::CoreMode).value<Settings::CoreMode>() == Settings::CoreMode::Server),
      m_control(control ? std::move(control) : deskflow::volume::makeSystemVolumeControl()),
      m_enable(new QCheckBox(this)),
      m_help(new QLabel(this)),
      m_device(new QLabel(this)),
      m_table(new QTableWidget(0, 2, this)),
      m_add(new QPushButton(this)),
      m_remove(new QPushButton(this))
{
  setObjectName("volumeSyncSettings");
  m_enable->setObjectName("cbSyncSystemVolume");
  m_table->setObjectName("volumeReferences");
  m_device->setObjectName("defaultAudioOutput");
  for (const auto &computer : config.computers()) {
    if (!computer.isNull())
      m_computers.append(computer.name());
  }
  m_computers.prepend(config.getServerName());
  m_computers.removeAll(QString{});
  m_computers.removeDuplicates();
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(m_enable);
  m_help->setWordWrap(true);
  m_device->setWordWrap(true);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->verticalHeader()->hide();
  m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_table->setMinimumHeight(125);
  layout->addWidget(m_help);
  layout->addWidget(m_table);
  auto *buttons = new QHBoxLayout;
  buttons->addWidget(m_add);
  buttons->addWidget(m_remove);
  buttons->addStretch();
  layout->addLayout(buttons);
  layout->addWidget(m_device);
  layout->addStretch();
  m_enable->setVisible(m_serverMode);
  m_table->setVisible(m_serverMode);
  m_add->setVisible(m_serverMode);
  m_remove->setVisible(m_serverMode);
  connect(m_enable, &QCheckBox::toggled, this, &VolumeSyncSettings::changed);
  connect(m_add, &QPushButton::clicked, this, &VolumeSyncSettings::addComputer);
  connect(m_remove, &QPushButton::clicked, this, [this] {
    if (m_table->currentRow() >= 0) {
      m_table->removeRow(m_table->currentRow());
      Q_EMIT changed();
    }
  });
  auto *timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this, [this] {
    if (isVisible())
      updateDevice();
  });
  timer->start(1500);
  retranslate();
  load();
}

void VolumeSyncSettings::retranslate()
{
  m_enable->setText(tr("Synchronize system volume and mute"));
  m_table->setHorizontalHeaderLabels({tr("Computer"), tr("Equal-loudness reference")});
  m_add->setText(tr("Add computer"));
  m_remove->setText(tr("Remove selected"));
  m_help->setText(
      m_serverMode
          ? tr("Enter the system volume each computer needs for the same perceived loudness. For example, A=100% and "
               "B=50% means A at 50% sets B to 25%. Changes work both ways; only receiving devices are capped at 100%. "
               "Mute is synchronized separately. New connections start from the server's current volume.")
          : tr("The server controls volume synchronization and the reference levels. When enabled there, this client "
               "automatically synchronizes its current default output volume and mute in both directions.")
  );
  updateDevice();
}

void VolumeSyncSettings::addRow(const QString &name, int reference)
{
  const int row = m_table->rowCount();
  m_table->insertRow(row);
  auto *item = new QTableWidgetItem(name);
  item->setToolTip(name);
  item->setFlags(item->flags() & ~Qt::ItemIsEditable);
  m_table->setItem(row, 0, item);
  auto *value = new QSpinBox(m_table);
  value->setRange(1, 100);
  value->setSuffix(" %");
  value->setValue(reference);
  m_table->setCellWidget(row, 1, value);
  connect(value, &QSpinBox::valueChanged, this, &VolumeSyncSettings::changed);
}

void VolumeSyncSettings::load()
{
  QSignalBlocker blocker(this);
  const auto parsed = deskflow::volume::parseWeights(Settings::value(Settings::Server::VolumeWeights).toString());
  const auto values = parsed.value_or(deskflow::volume::Weights{});
  m_enable->setChecked(parsed.has_value() && Settings::value(Settings::Core::SyncSystemVolume).toBool());
  auto names = m_computers;
  names.append(values.keys());
  names.removeDuplicates();
  m_table->setRowCount(0);
  for (const auto &name : names)
    addRow(name, deskflow::volume::reference(values, name));
  updateControls();
}

deskflow::volume::Weights VolumeSyncSettings::weights() const
{
  deskflow::volume::Weights values;
  for (int row = 0; row < m_table->rowCount(); ++row)
    values.insert(m_table->item(row, 0)->text(), qobject_cast<QSpinBox *>(m_table->cellWidget(row, 1))->value());
  return normalized(std::move(values));
}

void VolumeSyncSettings::save()
{
  if (!m_serverMode)
    return;
  Settings::setValue(Settings::Server::VolumeWeights, deskflow::volume::serializeWeights(weights()));
  Settings::setValue(Settings::Core::SyncSystemVolume, m_enable->isChecked());
}

bool VolumeSyncSettings::isModified() const
{
  if (!m_serverMode)
    return false;
  const auto stored = deskflow::volume::parseWeights(Settings::value(Settings::Server::VolumeWeights).toString());
  return !stored || normalized(*stored) != weights() ||
         m_enable->isChecked() != Settings::value(Settings::Core::SyncSystemVolume).toBool();
}

bool VolumeSyncSettings::isDefault() const
{
  return !m_serverMode || (!m_enable->isChecked() && weights().isEmpty());
}

void VolumeSyncSettings::resetToDefault()
{
  if (!m_serverMode)
    return;
  m_enable->setChecked(false);
  for (int row = 0; row < m_table->rowCount(); ++row)
    qobject_cast<QSpinBox *>(m_table->cellWidget(row, 1))->setValue(100);
  Q_EMIT changed();
}

void VolumeSyncSettings::updateControls()
{
  const bool enabled = m_serverMode && Settings::isWritable() && deskflow::volume::systemVolumeSupported();
  m_enable->setEnabled(enabled);
  m_table->setEnabled(enabled);
  m_add->setEnabled(enabled);
  m_remove->setEnabled(enabled);
}

void VolumeSyncSettings::addComputer()
{
  if (m_table->rowCount() >= 128)
    return;
  bool accepted = false;
  const auto name = QInputDialog::getText(
                        this, tr("Add computer"), tr("Computer name from the server layout or external configuration:"),
                        QLineEdit::Normal, {}, &accepted
  )
                        .trimmed();
  if (!accepted || name.isEmpty() || name.size() > 255 || name.contains(QChar(0)))
    return;
  for (int row = 0; row < m_table->rowCount(); ++row)
    if (m_table->item(row, 0)->text() == name)
      return;
  addRow(name, 100);
  Q_EMIT changed();
}

void VolumeSyncSettings::updateDevice()
{
  const auto output = m_control->read();
  if (!output) {
    m_device->setText(tr("No default audio output is available."));
  } else if (!(output->capabilities & deskflow::volume::Volume)) {
    m_device->setText(tr("%1: system volume cannot be adjusted on this output.").arg(output->deviceName));
  } else {
    auto text = tr("Default output: %1 — %2%").arg(output->deviceName).arg(output->state.volume / 100.0, 0, 'f', 0);
    if (output->state.muted)
      text += tr(" (muted)");
    if (!(output->capabilities & deskflow::volume::Mute))
      text += tr(" — system mute is not supported by this output");
    m_device->setText(text);
  }
}
