/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenAlignmentDialogTests.h"

#include "common/Settings.h"
#include "gui/config/ServerConfig.h"
#include "gui/dialogs/ScreenAlignmentDialog.h"
#include "gui/dialogs/ServerConfigDialog.h"
#include "gui/widgets/ScreenAlignmentPreview.h"
#include "gui/widgets/SettingsDialogButtonBox.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QStyle>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

#include <cmath>

namespace {
const ScreenAlignmentPair horizontal{"Windows", "Mac", Qt::Horizontal};
const ScreenAlignmentPair vertical{"Windows", "Laptop", Qt::Vertical};

template <class T> T *control(QObject &parent, const char *name)
{
  auto *result = parent.findChild<T *>(QLatin1String(name));
  if (!result)
    qFatal("Missing alignment test control: %s", name);
  return result;
}

void seed(const QMap<int, QString> &names = {{6, "Windows"}, {7, "Mac"}, {11, "Laptop"}})
{
  auto &settings = Settings::proxy();
  settings.beginGroup("internalConfig");
  settings.remove("screens");
  settings.beginWriteArray("screens");
  for (int i = 0; i < 15; ++i) {
    settings.setArrayIndex(i);
    settings.setValue("name", names.value(i));
  }
  settings.endArray();
  settings.endGroup();
}

void landscapeToPortrait(ScreenAlignmentDialog &dialog)
{
  control<QComboBox>(dialog, "alignmentPair")->setCurrentIndex(0);
  control<QCheckBox>(dialog, "enableAlignment")->setChecked(true);
  control<QDoubleSpinBox>(dialog, "firstWidthMm")->setValue(600);
  control<QDoubleSpinBox>(dialog, "firstHeightMm")->setValue(340);
  control<QDoubleSpinBox>(dialog, "secondWidthMm")->setValue(340);
  control<QDoubleSpinBox>(dialog, "secondHeightMm")->setValue(600);
  control<QPushButton>(dialog, "alignCenter")->click();
}

QDialogButtonBox *outerButtons(ServerConfigDialog &dialog)
{
  auto *box = dialog.findChild<SettingsDialogButtonBox *>();
  return box ? box->findChild<QDialogButtonBox *>() : nullptr;
}
} // namespace

void ScreenAlignmentDialogTests::init()
{
  Settings::proxy().clear();
  Settings::clearState();
  Settings::setValue(Settings::Core::ComputerName, "Windows");
  Settings::setValue(Settings::Server::GridWidth, 5);
  Settings::setValue(Settings::Server::GridHeight, 3);
  Settings::setValue(Settings::Server::ExternalConfig, false);
  seed();
}

void ScreenAlignmentDialogTests::saveAndCancel()
{
  ServerConfig config;
  const ServerConfig before(config);
  {
    ScreenAlignmentDialog dialog(nullptr, config);
    landscapeToPortrait(dialog);
    control<QPushButton>(dialog, "cancelAlignment")->click();
    QVERIFY(config == before);
  }
  {
    ScreenAlignmentDialog dialog(nullptr, config);
    landscapeToPortrait(dialog);
    control<QPushButton>(dialog, "saveAlignment")->click();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
  }
  const auto saved = config.screenAlignment(horizontal);
  QVERIFY(saved);
  QCOMPARE(saved->offsetMm, -130.0);
  QCOMPARE(saved->firstSizeMm, QSizeF(600, 340));
  QCOMPARE(saved->secondSizeMm, QSizeF(340, 600));
  ServerConfig untouchedSettings;
  QVERIFY(!untouchedSettings.screenAlignment(horizontal)); // Nested Save is staged only.
}

void ScreenAlignmentDialogTests::overlapAndReset()
{
  ServerConfig config;
  ScreenAlignmentDialog dialog(nullptr, config);
  landscapeToPortrait(dialog);
  auto *save = control<QPushButton>(dialog, "saveAlignment");
  QVERIFY(save->isEnabled());
  control<QDoubleSpinBox>(dialog, "offsetMm")->setValue(340);
  QVERIFY(!save->isEnabled());
  control<QPushButton>(dialog, "resetAlignment")->click();
  QVERIFY(!control<QCheckBox>(dialog, "enableAlignment")->isChecked());
  QVERIFY(save->isEnabled());
  save->click();
  QVERIFY(!config.screenAlignment(horizontal));
}

void ScreenAlignmentDialogTests::presetsAndPairSelection()
{
  ServerConfig config;
  ScreenAlignmentDialog dialog(nullptr, config);
  landscapeToPortrait(dialog);
  control<QPushButton>(dialog, "alignStart")->click();
  QCOMPARE(control<QDoubleSpinBox>(dialog, "offsetMm")->value(), 0.0);
  control<QPushButton>(dialog, "alignEnd")->click();
  QCOMPARE(control<QDoubleSpinBox>(dialog, "offsetMm")->value(), -260.0);
  control<QComboBox>(dialog, "alignmentPair")->setCurrentIndex(1);
  control<QCheckBox>(dialog, "enableAlignment")->setChecked(true);
  control<QDoubleSpinBox>(dialog, "firstWidthMm")->setValue(600);
  control<QDoubleSpinBox>(dialog, "secondWidthMm")->setValue(340);
  control<QPushButton>(dialog, "alignCenter")->click();
  QCOMPARE(control<QDoubleSpinBox>(dialog, "offsetMm")->value(), 130.0);
  control<QPushButton>(dialog, "saveAlignment")->click();
  QVERIFY(config.screenAlignment(horizontal));
  QVERIFY(config.screenAlignment(vertical));
  QCOMPARE(config.screenAlignment(horizontal)->offsetMm, -260.0);
  QCOMPARE(config.screenAlignment(vertical)->offsetMm, 130.0);
}

void ScreenAlignmentDialogTests::outerSaveAndCancel()
{
  ServerConfig config;
  for (bool saveOuter : {false, true}) {
    ServerConfigDialog outer(nullptr, config);
    auto *entry = control<QPushButton>(outer, "btnScreenAlignment");
    QVERIFY(entry->isEnabled());
    bool edited = false;
    QTimer::singleShot(0, &outer, [&] {
      auto *child = qobject_cast<ScreenAlignmentDialog *>(QApplication::activeModalWidget());
      if (!child) {
        if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
          modal->reject();
        return;
      }
      landscapeToPortrait(*child);
      control<QPushButton>(*child, "saveAlignment")->click();
      edited = child->result() == QDialog::Accepted;
    });
    entry->click();
    QVERIFY(edited);
    QVERIFY(!config.screenAlignment(horizontal));
    auto *buttons = outerButtons(outer);
    QVERIFY(buttons);
    auto *button = buttons->button(saveOuter ? QDialogButtonBox::Save : QDialogButtonBox::Cancel);
    QVERIFY(button);
    QVERIFY(button->isEnabled());
    button->click();
    QCOMPARE(config.screenAlignment(horizontal).has_value(), saveOuter);
    ServerConfig reloaded;
    QCOMPARE(reloaded.screenAlignment(horizontal).has_value(), saveOuter);
  }
}

void ScreenAlignmentDialogTests::offsetEditPreservesStoredDimensions()
{
  ServerConfig config;
  const ScreenAlignment saved{"Windows", "Mac", Qt::Horizontal, {600.123, 340.456}, {340.789, 600.321}, -100, true};
  QVERIFY(config.setScreenAlignment(saved));
  ScreenAlignmentDialog dialog(nullptr, config);
  control<QComboBox>(dialog, "alignmentPair")->setCurrentIndex(0);
  control<QDoubleSpinBox>(dialog, "offsetMm")->setValue(-125.5);
  control<QPushButton>(dialog, "saveAlignment")->click();
  const auto changed = config.screenAlignment(horizontal);
  QVERIFY(changed);
  QCOMPARE(changed->firstSizeMm, saved.firstSizeMm);
  QCOMPARE(changed->secondSizeMm, saved.secondSizeMm);
  QCOMPARE(changed->offsetMm, -125.5);
}

void ScreenAlignmentDialogTests::externalAndEmptyLayouts()
{
  Settings::setValue(Settings::Server::ExternalConfig, true);
  {
    ServerConfig config;
    ServerConfigDialog outer(nullptr, config);
    QVERIFY(!control<QPushButton>(outer, "btnScreenAlignment")->isEnabled());
  }
  Settings::setValue(Settings::Server::ExternalConfig, false);
  seed({{6, "Windows"}});
  ServerConfig config;
  ServerConfigDialog outer(nullptr, config);
  QVERIFY(!control<QPushButton>(outer, "btnScreenAlignment")->isEnabled());
  ScreenAlignmentDialog dialog(nullptr, config);
  QVERIFY(!control<QPushButton>(dialog, "saveAlignment")->isEnabled());
}

void ScreenAlignmentDialogTests::dragUsesOneAxis_data()
{
  QTest::addColumn<int>("orientation");
  QTest::newRow("side-by-side") << int(Qt::Horizontal);
  QTest::newRow("stacked") << int(Qt::Vertical);
}

void ScreenAlignmentDialogTests::compactFeedbackStaysVisible()
{
  ServerConfig config;
  ScreenAlignmentDialog dialog(nullptr, config);
  landscapeToPortrait(dialog);
  dialog.resize(660, 520);
  dialog.show();
  QCoreApplication::processEvents();
  const auto visibleInDialog = [&](QWidget *widget) {
    return widget->isVisible() && dialog.rect().contains(QRect(widget->mapTo(&dialog, QPoint{}), widget->size()));
  };
  auto *mapping = control<QLabel>(dialog, "alignmentMapping");
  auto *status = control<QLabel>(dialog, "alignmentStatus");
  QVERIFY(visibleInDialog(mapping));
  QVERIFY(visibleInDialog(status));
  const QString validStatus = status->text();
  control<QDoubleSpinBox>(dialog, "offsetMm")->setValue(340);
  QCoreApplication::processEvents();
  QVERIFY(!control<QPushButton>(dialog, "saveAlignment")->isEnabled());
  QVERIFY(!status->text().isEmpty());
  QVERIFY(status->text() != validStatus);
  QVERIFY(visibleInDialog(status));
  QVERIFY(visibleInDialog(control<QPushButton>(dialog, "cancelAlignment")));
}

void ScreenAlignmentDialogTests::dragUsesOneAxis()
{
  QFETCH(int, orientation);
  ScreenAlignment alignment{"Windows", "Mac", Qt::Orientation(orientation), {600, 340}, {340, 600}, 0, true};
  ScreenAlignmentPreview preview;
  preview.resize(800, 320);
  preview.setAlignment(alignment);
  preview.show();
  QCoreApplication::processEvents();
  const auto start = preview.secondScreenRect().center().toPoint();
  const double scale = preview.pixelsPerMm();
  QSignalSpy changes(&preview, &ScreenAlignmentPreview::offsetEdited);
  connect(&preview, &ScreenAlignmentPreview::offsetEdited, &preview, [&](double value) {
    alignment.offsetMm = value;
    preview.setAlignment(alignment); // Real dialog follows this feedback path.
  });
  QTest::mousePress(&preview, Qt::LeftButton, Qt::NoModifier, start);
  for (int delta : {20, 100}) {
    const QPoint movement = orientation == Qt::Horizontal ? QPoint(35, delta) : QPoint(delta, 35);
    const QPoint end = start + movement;
    QMouseEvent event(QEvent::MouseMove, end, preview.mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&preview, &event);
    QVERIFY(!changes.isEmpty());
    // The editor exposes tenths of a millimetre; the drag may quantize by half a step.
    QVERIFY(std::abs(changes.last().at(0).toDouble() - delta / scale) <= 0.050001);
    QCOMPARE(preview.pixelsPerMm(), scale); // Moving must not change the drag's coordinate system.
  }
  QTest::mouseRelease(&preview, Qt::LeftButton, Qt::NoModifier, start);
}

void ScreenAlignmentDialogTests::renderExamples()
{
  const auto destination = qEnvironmentVariable("DESKFLOW_ALIGNMENT_RENDER_DIR");
  if (destination.isEmpty())
    QSKIP("Set DESKFLOW_ALIGNMENT_RENDER_DIR to generate visual review artifacts");
  QVERIFY(QDir().mkpath(destination));
  QTranslator translator;
  QTranslator qtTranslator;
  const auto qtQm = qEnvironmentVariable("DESKFLOW_ALIGNMENT_QT_QM");
  if (!qtQm.isEmpty()) {
    QVERIFY(qtTranslator.load(qtQm));
    qApp->installTranslator(&qtTranslator);
  }
  const auto qm = qEnvironmentVariable("DESKFLOW_ALIGNMENT_QM");
  if (!qm.isEmpty()) {
    QVERIFY(translator.load(qm));
    qApp->installTranslator(&translator);
  }
  const QPalette originalPalette = qApp->palette();
  for (const QString theme : {QStringLiteral("light"), QStringLiteral("dark")}) {
    QPalette palette = qApp->style()->standardPalette();
    if (theme == "dark") {
      palette.setColor(QPalette::Window, QColor(35, 38, 43));
      palette.setColor(QPalette::Base, QColor(25, 28, 32));
      palette.setColor(QPalette::Button, QColor(48, 52, 58));
      for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(role, QColor(235, 237, 240));
      palette.setColor(QPalette::Highlight, QColor(80, 150, 235));
      palette.setColor(QPalette::HighlightedText, Qt::white);
    }
    qApp->setPalette(palette);
    ServerConfig config;
    ScreenAlignmentDialog dialog(nullptr, config);
    landscapeToPortrait(dialog);
    dialog.show();
    QTest::qWait(50);
    QVERIFY(dialog.grab().save(destination + "/alignment-" + theme + ".png"));
    dialog.resize(660, 520);
    QTest::qWait(20);
    QVERIFY(dialog.grab().save(destination + "/alignment-" + theme + "-compact.png"));
    control<QDoubleSpinBox>(dialog, "offsetMm")->setValue(340);
    QTest::qWait(20);
    QVERIFY(dialog.grab().save(destination + "/alignment-" + theme + "-no-overlap.png"));
  }
  qApp->setPalette(originalPalette);
  if (!qm.isEmpty())
    qApp->removeTranslator(&translator);
  if (!qtQm.isEmpty())
    qApp->removeTranslator(&qtTranslator);
}

int main(int argc, char **argv)
{
  QApplication application(argc, argv);
  application.setStyle("fusion");
  for (const auto &font : qEnvironmentVariable("DESKFLOW_ALIGNMENT_FONT_FILES").split(';', Qt::SkipEmptyParts)) {
    if (QFontDatabase::addApplicationFont(font) < 0)
      return 3;
  }
  const auto family = qEnvironmentVariable("DESKFLOW_ALIGNMENT_FONT_FAMILY");
  if (!family.isEmpty())
    application.setFont(QFont(family, 9));
  static QTemporaryDir isolated; // Constructed before Settings; removed after its final flush.
  if (!isolated.isValid())
    return 2;
  const QString configRoot = isolated.path() + "/config";
  const QString stateRoot = isolated.path() + "/state";
  QDir().mkpath(configRoot + "/Deskflow");
  QDir().mkpath(stateRoot);
  qputenv("XDG_CONFIG_HOME", configRoot.toUtf8());
  qputenv("XDG_STATE_HOME", stateRoot.toUtf8());
#ifdef Q_OS_WIN
  const QString bootstrap = QCoreApplication::applicationDirPath() + "/settings/Deskflow.conf";
  QDir().mkpath(QFileInfo(bootstrap).absolutePath());
  QFile portable(bootstrap);
  if (!portable.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    return 2;
  portable.close();
#endif
  Settings::setSettingsFile(configRoot + "/Deskflow/Deskflow.conf");
  Settings::setStateFile(stateRoot + "/Deskflow.state");
  ScreenAlignmentDialogTests tests;
  const int result = QTest::qExec(&tests, argc, argv);
  Settings::save(false);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
#ifdef Q_OS_WIN
  QFile::remove(bootstrap);
#endif
  return result;
}
