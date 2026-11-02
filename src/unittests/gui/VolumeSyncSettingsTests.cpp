/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "../common/FakeVolumeControl.h"
#include "common/Settings.h"
#include "gui/config/ServerConfig.h"
#include "gui/widgets/VolumeSyncSettings.h"
#include <QCheckBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>

class VolumeSyncSettingsTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void referenceEditingPersistsOnlyOnAcceptAndNeverSetsAudio()
  {
    QTemporaryDir directory;
    Settings::setSettingsFile(directory.filePath("settings.conf"));
    Settings::setStateFile(directory.filePath("state.conf"));
    Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Server);
    Settings::setValue(Settings::Core::ComputerName, "A");
    Settings::setValue(Settings::Server::VolumeWeights, QStringLiteral(R"({"B":50})"));
    ServerConfig config;
    auto control = std::make_unique<FakeVolumeControl>();
    auto *audio = control.get();
    VolumeSyncSettings widget(config, nullptr, std::move(control));
    QVERIFY(!widget.isModified());
    auto *toggle = widget.findChild<QCheckBox *>("cbSyncSystemVolume");
    auto *table = widget.findChild<QTableWidget *>("volumeReferences");
    QVERIFY(toggle && table);
    QVERIFY(!toggle->isHidden());
    QVERIFY(!toggle->isChecked());
    QSpinBox *b = nullptr;
    for (int row = 0; row < table->rowCount(); ++row)
      if (table->item(row, 0)->text() == "B")
        b = qobject_cast<QSpinBox *>(table->cellWidget(row, 1));
    QVERIFY(b);
    QCOMPARE(b->minimum(), 1);
    QCOMPARE(b->maximum(), 100);
    QCOMPARE(b->value(), 50);
    b->setValue(25);
    toggle->setChecked(true);
    QVERIFY(widget.isModified());
    QCOMPARE(
        deskflow::volume::parseWeights(Settings::value(Settings::Server::VolumeWeights).toString())->value("B"), 50
    );
    QCOMPARE(audio->writes, 0);
    widget.load(); // cancel/reset discards edits
    QVERIFY(!widget.isModified());
    toggle->setChecked(true);
    widget.save();
    QVERIFY(Settings::value(Settings::Core::SyncSystemVolume).toBool());
    QCOMPARE(widget.weights().value("B"), 50);
    QCOMPARE(audio->writes, 0);
    widget.resetToDefault();
    QVERIFY(widget.isDefault());
    widget.save();
    QVERIFY(!Settings::value(Settings::Core::SyncSystemVolume).toBool());
    QCOMPARE(Settings::value(Settings::Server::VolumeWeights).toString(), QStringLiteral("{}"));
  }

  void clientCannotChangeServerVolumePolicy()
  {
    QTemporaryDir directory;
    Settings::setSettingsFile(directory.filePath("settings.conf"));
    Settings::setStateFile(directory.filePath("state.conf"));
    Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Client);
    Settings::setValue(Settings::Core::SyncSystemVolume, true);
    ServerConfig config;
    VolumeSyncSettings widget(config, nullptr, std::make_unique<FakeVolumeControl>());
    auto *toggle = widget.findChild<QCheckBox *>("cbSyncSystemVolume");
    QVERIFY(toggle->isHidden());
    toggle->setChecked(false);
    widget.save();
    QVERIFY(!widget.isModified());
    QVERIFY(Settings::value(Settings::Core::SyncSystemVolume).toBool());
  }
};
QTEST_MAIN(VolumeSyncSettingsTests)
#include "VolumeSyncSettingsTests.moc"
