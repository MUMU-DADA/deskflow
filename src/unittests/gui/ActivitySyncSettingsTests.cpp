/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "common/Settings.h"
#include "gui/config/ServerConfig.h"
#include "gui/dialogs/SettingsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

class ActivitySyncSettingsTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void settingPersistsAndCancelKeepsPreviousValue()
  {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    Settings::setSettingsFile(temporary.filePath("settings.conf"));
    Settings::setStateFile(temporary.filePath("state.conf"));
    Settings::setValue(Settings::Core::PreventSleep, false);
    ServerConfig config;
    {
      SettingsDialog dialog(nullptr, config);
      auto *toggle = dialog.findChild<QCheckBox *>("cbSyncUserActivity");
      QVERIFY(toggle);
      QVERIFY(!toggle->isChecked());
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
      QVERIFY(toggle->isEnabled());
#else
      QVERIFY(!toggle->isEnabled());
#endif
      toggle->setChecked(true);
      QVERIFY(QMetaObject::invokeMethod(&dialog, "accept", Qt::DirectConnection));
      QVERIFY(Settings::value(Settings::Core::SyncUserActivity).toBool());
      QVERIFY(!Settings::value(Settings::Core::PreventSleep).toBool());
    }
    {
      SettingsDialog dialog(nullptr, config);
      auto *toggle = dialog.findChild<QCheckBox *>("cbSyncUserActivity");
      QVERIFY(toggle->isChecked());
      toggle->setChecked(false);
      dialog.reject();
      QVERIFY(Settings::value(Settings::Core::SyncUserActivity).toBool());
    }
    {
      SettingsDialog dialog(nullptr, config);
      auto *buttons = dialog.findChild<QDialogButtonBox *>();
      QVERIFY(buttons);
      auto *restore = buttons->button(QDialogButtonBox::RestoreDefaults);
      QVERIFY(restore);
      restore->click();
      QVERIFY(!dialog.findChild<QCheckBox *>("cbSyncUserActivity")->isChecked());
      QVERIFY(QMetaObject::invokeMethod(&dialog, "accept", Qt::DirectConnection));
      QVERIFY(!Settings::value(Settings::Core::SyncUserActivity).toBool());
    }
  }
};

QTEST_MAIN(ActivitySyncSettingsTests)
#include "ActivitySyncSettingsTests.moc"
