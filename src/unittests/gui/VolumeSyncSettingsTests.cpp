/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "../common/FakeVolumeControl.h"
#include "common/Settings.h"
#include "gui/config/ServerConfig.h"
#include "gui/widgets/VolumeSyncSettings.h"
#include <QCheckBox>
#include <QDir>
#include <QFontDatabase>
#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>

class VolumeSyncSettingsTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void simplifiedChineseUsesCompiledCatalog()
  {
    QTemporaryDir directory;
    Settings::setSettingsFile(directory.filePath("settings.conf"));
    Settings::setStateFile(directory.filePath("state.conf"));
    Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Server);
    Settings::setValue(Settings::Core::ComputerName, "A");
    for (const auto &file : qEnvironmentVariable("DESKFLOW_VOLUME_FONT_FILES").split(';', Qt::SkipEmptyParts)) {
      const int fontId = QFontDatabase::addApplicationFont(file);
      QVERIFY(fontId >= 0);
      qApp->setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).first(), 10));
    }
    // An override also lets the same regression check validate a packaged QM.
    const auto qm = qEnvironmentVariable(
        "DESKFLOW_VOLUME_TEST_QM", QString::fromUtf8(DESKFLOW_TEST_TRANSLATIONS_DIR) + "/deskflow_zh_CN.qm"
    );
    QTranslator translator;
    QVERIFY2(translator.load(qm), qPrintable(qm));
    QVERIFY(qApp->installTranslator(&translator));
    const auto cleanup = qScopeGuard([&] { qApp->removeTranslator(&translator); });
    QCOMPARE(QCoreApplication::translate("SettingsDialog", "Volume sync"), QStringLiteral("音量同步"));

    ServerConfig config;
    auto control = std::make_unique<FakeVolumeControl>();
    auto *audio = control.get();
    audio->current.state.muted = true;
    VolumeSyncSettings widget(config, nullptr, std::move(control));
    auto *toggle = widget.findChild<QCheckBox *>("cbSyncSystemVolume");
    auto *table = widget.findChild<QTableWidget *>("volumeReferences");
    auto *device = widget.findChild<QLabel *>("defaultAudioOutput");
    QVERIFY(toggle && table && device);
    QCOMPARE(toggle->text(), QStringLiteral("同步系统音量与静音"));
    QCOMPARE(table->horizontalHeaderItem(0)->text(), QStringLiteral("计算机"));
    QCOMPARE(table->horizontalHeaderItem(1)->text(), QStringLiteral("等响度参考音量"));
    QCOMPARE(device->text(), QStringLiteral("默认输出：Test speakers — 40%（已静音）"));
    QStringList buttons;
    for (const auto *button : widget.findChildren<QPushButton *>())
      buttons.append(button->text());
    QVERIFY(buttons.contains(QStringLiteral("添加计算机")));
    QVERIFY(buttons.contains(QStringLiteral("移除所选项")));
    QString help;
    for (const auto *label : widget.findChildren<QLabel *>())
      if (label != device)
        help += label->text();
    QVERIFY(help.contains(QStringLiteral("相同听感响度")));
    QVERIFY(help.contains(QStringLiteral("服务端当前音量")));
    QCOMPARE(
        QCoreApplication::translate(
            "VolumeSyncSettings", "Computer name from the server layout or external configuration:"
        ),
        QStringLiteral("服务端屏幕布局或外部配置中的计算机名称：")
    );

    const auto renderDir = qEnvironmentVariable("DESKFLOW_VOLUME_RENDER_DIR");
    if (!renderDir.isEmpty()) {
      QVERIFY(QDir().mkpath(renderDir));
      widget.resize(640, 390);
      widget.show();
      QTest::qWait(30);
      QVERIFY(widget.grab().save(renderDir + "/volume-sync-zh_CN.png"));
    }

    audio->current.capabilities = deskflow::volume::Volume;
    audio->current.state.muted = false;
    widget.retranslate();
    QVERIFY(device->text().endsWith(QStringLiteral("此输出设备不支持系统静音")));
    audio->current.capabilities = 0;
    widget.retranslate();
    QCOMPARE(device->text(), QStringLiteral("Test speakers：此输出设备不支持调节系统音量。"));
    audio->present = false;
    widget.retranslate();
    QCOMPARE(device->text(), QStringLiteral("没有可用的默认音频输出设备。"));
    QCOMPARE(audio->writes, 0);

    Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Client);
    VolumeSyncSettings client(config, nullptr, std::make_unique<FakeVolumeControl>());
    help.clear();
    for (const auto *label : client.findChildren<QLabel *>())
      if (label->objectName() != "defaultAudioOutput")
        help += label->text();
    QVERIFY(help.contains(QStringLiteral("由服务端统一设置")));
    QVERIFY(help.contains(QStringLiteral("双向同步")));
  }

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
