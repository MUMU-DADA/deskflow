/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenCalibrationTests.h"

#include "gui/core/ScreenCalibrationController.h"
#include "gui/dialogs/ScreenCalibrationDialog.h"

#include <QCheckBox>
#include <QDir>
#include <QFontDatabase>
#include <QHash>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyle>
#include <QTranslator>
#include <QUuid>

#include <cmath>

namespace {
ScreenAlignment alignment()
{
  ScreenAlignment value;
  value.firstName = "Windows";
  value.secondName = "Mac";
  value.orientation = Qt::Horizontal;
  value.enabled = true;
  value.calibration = ScreenEdgeCalibration{{0.25, 0.3125}, {0.75, 0.5625}, 16.0 / 9.0, 9.0 / 16.0};
  return value;
}

QJsonObject response(const QJsonObject &show, const QString &type, const QJsonArray &points, double sequence)
{
  return {
      {"session", show.value("session")},
      {"source", show.value("target")},
      {"type", type},
      {"revision", show.value("revision")},
      {"seq", sequence},
      {"points", points},
      {"screen", QJsonObject{{"width", 1920}, {"height", 1080}, {"dpr", 1}, {"screenCount", 1}}}
  };
}

// Only endpoint responses enter receiveMessage. In particular, never loop a
// "show" request back into the controller: these tests do not create overlays,
// read Settings, send network traffic, or capture real input.
struct Session
{
  QList<QJsonObject> sent;
  QHash<QString, qint64> sequences;
  bool automaticReplies = false;
  QWidget window;
  ScreenCalibrationController controller{&window};

  Session()
  {
    QObject::connect(
        &controller, &ScreenCalibrationController::sendMessage, &controller,
        [this](const QJsonObject &message) {
          sent.append(message);
          if (automaticReplies && message.value("type").toString() == "show") {
            const double sequence = ++sequences[message.value("target").toString()];
            const auto reply = response(message, "ready", message.value("points").toArray(), sequence);
            QTimer::singleShot(0, &controller, [this, reply] { controller.receiveMessage(reply); });
          }
        }
    );
    controller.receiveMessage({{"type", "capabilities"}, {"source", "broker"}});
  }

  bool start()
  {
    controller.setTransportAvailable(true);
    return controller.start(alignment(), &window);
  }

  QJsonObject show(const QString &target) const
  {
    for (auto it = sent.crbegin(); it != sent.crend(); ++it) {
      if (it->value("type").toString() == "show" && it->value("target").toString() == target)
        return *it;
    }
    return {};
  }

  void readyBoth(double sequence = 1)
  {
    controller.receiveMessage(response(show("Windows"), "ready", {0.25, 0.75, 0.5}, sequence));
    controller.receiveMessage(response(show("Mac"), "ready", {0.3125, 0.5625, 0.4375}, sequence));
  }

  int count(const QString &type) const
  {
    int result = 0;
    for (const auto &message : sent)
      result += message.value("type").toString() == type;
    return result;
  }

  void fail(const QString &reason)
  {
    controller.receiveMessage(
        {{"session", show("Windows").value("session")}, {"source", "broker"}, {"type", "error"}, {"reason", reason}}
    );
  }
};

template <class T> T *control(QWidget &widget, const char *name)
{
  auto *result = widget.findChild<T *>(QString::fromLatin1(name));
  if (!result)
    qFatal("Missing calibration dialog control: %s", name);
  return result;
}
} // namespace

void ScreenCalibrationTests::unavailableStart()
{
  Session session;
  QVERIFY(!session.controller.start(alignment(), &session.window));
  QVERIFY(!session.controller.isActive());
  QVERIFY(!session.controller.result());
  QVERIFY(!session.controller.canAccept());
  QVERIFY(!session.controller.statusText().isEmpty());
  QVERIFY(session.sent.isEmpty());
}

void ScreenCalibrationTests::readyMappingAndCancellation()
{
  Session session;
  const auto original = alignment();
  session.controller.setTransportAvailable(true);
  QVERIFY(session.controller.start(original, &session.window));
  QCOMPARE(session.count("show"), 2);
  const auto first = session.show("Windows");
  const auto second = session.show("Mac");
  QCOMPARE(first.value("edge").toString(), "right");
  QCOMPARE(second.value("edge").toString(), "left");
  QVERIFY(!QUuid(first.value("session").toString()).isNull());
  session.controller.receiveMessage(response(first, "ready", {0.25, 0.75, 0.5}, 1));
  QVERIFY(!session.controller.canAccept());
  auto remote = response(second, "ready", {0.3125, 0.5625, 0.4375}, 1);
  remote.insert("screen", QJsonObject{{"width", 1080}, {"height", 1920}, {"dpr", 2}, {"screenCount", 1}});
  session.controller.receiveMessage(remote);
  QVERIFY(session.controller.canAccept());
  const auto result = session.controller.result();
  QVERIFY(result);
  QCOMPARE(result->transform()->ratio, 2.0);
  QCOMPARE(result->transform()->offset, -0.375);
  QCOMPARE(result->firstAspectRatio, 1920.0 / 1080);
  QCOMPARE(result->secondAspectRatio, 1080.0 / 1920);
  QVERIFY(!session.controller.measurementText().isEmpty());
  QVERIFY(original == alignment()); // The controller never mutates the caller's draft.

  session.sent.clear();
  session.controller.cancel();
  QVERIFY(!session.controller.isActive());
  QVERIFY(!session.controller.result());
  QVERIFY(!session.controller.canAccept());
  QCOMPARE(session.count("close"), 2);
  QCOMPARE(session.sent[0].value("target").toString(), "Windows");
  QCOMPARE(session.sent[1].value("target").toString(), "Mac");
  QCOMPARE(session.sent[0].value("session"), first.value("session"));
  session.controller.cancel();
  QCOMPARE(session.count("close"), 2); // Cleanup is idempotent.
}

void ScreenCalibrationTests::invalidGeometry_data()
{
  QTest::addColumn<QString>("key");
  QTest::addColumn<double>("value");
  QTest::newRow("empty-width") << "width" << 0.0;
  QTest::newRow("empty-height") << "height" << 0.0;
  QTest::newRow("oversize") << "width" << 100001.0;
  QTest::newRow("zero-scale") << "dpr" << 0.0;
  QTest::newRow("oversize-scale") << "dpr" << 17.0;
  QTest::newRow("multiple-screens") << "screenCount" << 2.0;
}

void ScreenCalibrationTests::invalidGeometry()
{
  QFETCH(QString, key);
  QFETCH(double, value);
  Session session;
  QSignalSpy cancelled(&session.controller, &ScreenCalibrationController::cancelled);
  QVERIFY(session.start());
  auto invalid = response(session.show("Windows"), "ready", {0.25, 0.75, 0.5}, 1);
  auto geometry = invalid.value("screen").toObject();
  geometry.insert(key, value);
  invalid.insert("screen", geometry);
  session.controller.receiveMessage(invalid);
  QCOMPARE(cancelled.size(), 1);
  QVERIFY(!session.controller.isActive());
  QVERIFY(!session.controller.canAccept());
  QCOMPARE(session.count("close"), 2);
}

void ScreenCalibrationTests::staleMessagesAndRevisions()
{
  Session session;
  QVERIFY(session.start());
  const auto oldShow = session.show("Windows");
  auto stale = response(oldShow, "ready", {0.25, 0.75, 0.5}, 1);
  stale.insert("session", QUuid::createUuid().toString(QUuid::WithoutBraces));
  session.controller.receiveMessage(stale);
  QVERIFY(!session.controller.firstEndpoint().ready);
  auto stranger = response(oldShow, "ready", {0.25, 0.75, 0.5}, 1);
  stranger.insert("source", "unrelated-peer");
  session.controller.receiveMessage(stranger);
  QVERIFY(!session.controller.firstEndpoint().ready);
  stranger.insert("type", "cancel");
  session.controller.receiveMessage(stranger);
  QVERIFY(session.controller.isActive());
  for (double sequence : {-1.0, 0.5, 9007199254740992.0}) {
    session.controller.receiveMessage(response(oldShow, "ready", {0.25, 0.75, 0.5}, sequence));
    QVERIFY(!session.controller.firstEndpoint().ready);
  }
  session.readyBoth();
  QVERIFY(session.controller.canAccept());
  const auto original = session.controller.result();
  session.controller.receiveMessage(response(oldShow, "points", {0.4, 0.6, 0.5}, 1));
  QVERIFY(session.controller.result() == original);

  session.controller.setCheckPointEnabled(true);
  const auto currentShow = session.show("Windows");
  QVERIFY(currentShow.value("revision").toInteger() > oldShow.value("revision").toInteger());
  QVERIFY(!session.controller.firstEndpoint().ready);
  QVERIFY(!session.controller.secondEndpoint().ready);
  session.controller.receiveMessage(response(oldShow, "ready", {0.25, 0.75, 0.5}, 100));
  QVERIFY(!session.controller.firstEndpoint().ready);
  session.readyBoth(2);
  QVERIFY(session.controller.canAccept());
  session.controller.receiveMessage(response(oldShow, "points", {0.4, 0.6, 0.5}, 200));
  QVERIFY(session.controller.result() == original);
  session.controller.receiveMessage(response(currentShow, "points", {0.3, 0.8, 0.55}, 3));
  QCOMPARE(session.controller.firstEndpoint().sequence, qint64(3));
  QCOMPARE(session.controller.firstEndpoint().points[0], 0.3);
}

void ScreenCalibrationTests::checkPointRequiresThreePositions()
{
  Session session;
  QVERIFY(session.start());
  session.readyBoth();
  session.controller.setCheckPointEnabled(true);
  session.controller.receiveMessage(response(session.show("Windows"), "ready", {0.25, 0.75}, 2));
  QVERIFY(!session.controller.firstEndpoint().ready);
  QVERIFY(!session.controller.canAccept());
  session.readyBoth(3);
  QVERIFY(session.controller.canAccept());
  const auto before = session.controller.firstEndpoint().points;
  session.controller.receiveMessage(response(session.show("Windows"), "points", {0.3, 0.8}, 4));
  QCOMPARE(session.controller.firstEndpoint().points, before);
  QCOMPARE(session.controller.firstEndpoint().sequence, qint64(3));
}

void ScreenCalibrationTests::checkPointDoesNotRefit()
{
  Session session;
  QVERIFY(session.start());
  session.readyBoth();
  const auto fitted = session.controller.result();
  session.controller.setCheckPointEnabled(true);
  session.controller.receiveMessage(response(session.show("Windows"), "ready", {0.25, 0.75, 0.5}, 2));
  session.controller.receiveMessage(response(session.show("Mac"), "ready", {0.3125, 0.5625, 0.5}, 2));
  QVERIFY(session.controller.result() == fitted);
  QVERIFY(!session.controller.canAccept());
  QVERIFY(session.controller.measurementText().contains("12.50"));
  session.controller.receiveMessage(response(session.show("Mac"), "points", {0.3125, 0.5625, 0.4375}, 3));
  QVERIFY(session.controller.canAccept());
  QVERIFY(session.controller.result() == fitted);
}

void ScreenCalibrationTests::geometryChangesAbort()
{
  Session session;
  QSignalSpy cancelled(&session.controller, &ScreenCalibrationController::cancelled);
  QVERIFY(session.start());
  session.readyBoth();
  session.controller.setCheckPointEnabled(true);
  auto changed = response(session.show("Windows"), "ready", {0.25, 0.75, 0.5}, 2);
  changed.insert("screen", QJsonObject{{"width", 2560}, {"height", 1440}, {"dpr", 1}, {"screenCount", 1}});
  session.controller.receiveMessage(changed);
  QCOMPARE(cancelled.size(), 1);
  QVERIFY(!session.controller.isActive());
  QVERIFY(!session.controller.canAccept());
}

void ScreenCalibrationTests::timeoutErrorAndRetry()
{
  Session session;
  QSignalSpy cancelled(&session.controller, &ScreenCalibrationController::cancelled);
  QVERIFY(session.start());
  const auto originalSession = session.show("Windows").value("session");
  session.fail("timeout");
  QCOMPARE(cancelled.size(), 1);
  QVERIFY(!session.controller.isActive());
  QCOMPARE(session.count("close"), 2);
  QVERIFY(session.start());
  QVERIFY(session.show("Windows").value("session") != originalSession);
  session.controller.receiveMessage({{"session", originalSession}, {"source", "broker"}, {"type", "cancel"}});
  QVERIFY(session.controller.isActive());
  session.readyBoth();
  QVERIFY(session.controller.canAccept());
  session.controller.setTransportAvailable(false);
  QCOMPARE(cancelled.size(), 2);
  QVERIFY(!session.controller.isActive());
  QVERIFY(!session.controller.canAccept());
  QVERIFY(session.start());
}

void ScreenCalibrationTests::wizardAcceptAndCancel()
{
  Session session;
  session.controller.setTransportAvailable(true);
  session.automaticReplies = true;
  const auto original = alignment();
  {
    ScreenCalibrationDialog dialog(nullptr, session.controller, original);
    dialog.show();
    auto *use = control<QPushButton>(dialog, "useCalibration");
    QTRY_VERIFY(use->isEnabled());
    const auto expected = session.controller.result();
    QTest::mouseClick(use, Qt::LeftButton);
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QVERIFY(dialog.calibration() == expected);
    QVERIFY(!session.controller.isActive());
    QCOMPARE(session.count("close"), 2);
  }
  {
    ScreenCalibrationDialog dialog(nullptr, session.controller, original);
    dialog.show();
    QTRY_VERIFY(control<QPushButton>(dialog, "useCalibration")->isEnabled());
    QTest::keyClick(&dialog, Qt::Key_Escape);
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    QVERIFY(!dialog.calibration());
    QVERIFY(!session.controller.isActive());
    QCOMPARE(session.count("close"), 4);
  }
  QVERIFY(original == alignment());
}

void ScreenCalibrationTests::wizardFailureAndRetry()
{
  Session session;
  session.controller.setTransportAvailable(true);
  ScreenCalibrationDialog dialog(nullptr, session.controller, alignment());
  dialog.show();
  QTRY_VERIFY(session.controller.isActive());
  session.fail("timeout");
  auto *retry = control<QPushButton>(dialog, "retryCalibration");
  QVERIFY(retry->isVisible());
  QVERIFY(retry->isEnabled());
  QVERIFY(!control<QPushButton>(dialog, "useCalibration")->isEnabled());
  QVERIFY(!control<QLabel>(dialog, "calibrationStatus")->text().isEmpty());
  session.automaticReplies = true;
  QTest::mouseClick(retry, Qt::LeftButton);
  QTRY_VERIFY(control<QPushButton>(dialog, "useCalibration")->isEnabled());
  QTest::mouseClick(control<QPushButton>(dialog, "cancelCalibration"), Qt::LeftButton);
  QVERIFY(!dialog.calibration());
  QVERIFY(!session.controller.isActive());
}

void ScreenCalibrationTests::failedWizardCannotCancelAnotherSession()
{
  Session session;
  QVERIFY(session.start());
  session.readyBoth();
  const auto original = session.controller.result();
  const auto originalStatus = session.controller.statusText();
  session.sent.clear();
  {
    ScreenCalibrationDialog dialog(nullptr, session.controller, alignment());
    dialog.show();
    QTRY_VERIFY(control<QPushButton>(dialog, "retryCalibration")->isVisible());
    QVERIFY(!control<QPushButton>(dialog, "useCalibration")->isEnabled());
    QVERIFY(!control<QPushButton>(dialog, "retryCalibration")->isEnabled());
    QVERIFY(control<QLabel>(dialog, "calibrationStatus")->text() != originalStatus);
    dialog.reject();
    QVERIFY(!dialog.calibration());
  }
  QVERIFY(session.controller.isActive());
  QVERIFY(session.controller.result() == original);
  QCOMPARE(session.controller.statusText(), originalStatus);
  QCOMPARE(session.count("close"), 0);
}

void ScreenCalibrationTests::compactFeedbackStaysVisible()
{
  Session session;
  session.automaticReplies = true;
  session.controller.setTransportAvailable(true);
  ScreenCalibrationDialog dialog(nullptr, session.controller, alignment());
  dialog.resize(560, 420);
  dialog.show();
  QTRY_VERIFY(control<QPushButton>(dialog, "useCalibration")->isEnabled());
  const auto visible = [&](QWidget *widget) {
    return widget->isVisible() && dialog.rect().contains(QRect(widget->mapTo(&dialog, QPoint{}), widget->size()));
  };
  for (const auto *name :
       {"calibrationFirstState", "calibrationSecondState", "calibrationMeasurement", "calibrationStatus"})
    QVERIFY(visible(control<QLabel>(dialog, name)));
  QVERIFY(visible(control<QPushButton>(dialog, "cancelCalibration")));
  session.controller.receiveMessage(response(session.show("Mac"), "points", {0.3, 0.31, 0.305}, 100));
  QCoreApplication::processEvents();
  QVERIFY(!control<QPushButton>(dialog, "useCalibration")->isEnabled());
  QVERIFY(visible(control<QLabel>(dialog, "calibrationStatus")));
  QVERIFY(!control<QLabel>(dialog, "calibrationPreviewHint")->isHidden());
}

void ScreenCalibrationTests::renderExamples()
{
  const auto destination = qEnvironmentVariable("DESKFLOW_CALIBRATION_RENDER_DIR");
  if (destination.isEmpty())
    QSKIP("Set DESKFLOW_CALIBRATION_RENDER_DIR to generate visual review artifacts");
  QVERIFY(QDir().mkpath(destination));
  for (const auto &font : qEnvironmentVariable("DESKFLOW_ALIGNMENT_FONT_FILES").split(';', Qt::SkipEmptyParts))
    QVERIFY(QFontDatabase::addApplicationFont(font) >= 0);
  const auto family = qEnvironmentVariable("DESKFLOW_ALIGNMENT_FONT_FAMILY");
  if (!family.isEmpty())
    qApp->setFont(QFont(family, 9));
  qApp->setStyle("fusion");
  QTranslator translator;
  QTranslator qtTranslator;
  const auto qm = qEnvironmentVariable("DESKFLOW_ALIGNMENT_QM");
  const auto qtQm = qEnvironmentVariable("DESKFLOW_ALIGNMENT_QT_QM");
  if (!qm.isEmpty()) {
    QVERIFY(translator.load(qm));
    qApp->installTranslator(&translator);
  }
  if (!qtQm.isEmpty()) {
    QVERIFY(qtTranslator.load(qtQm));
    qApp->installTranslator(&qtTranslator);
  }
  const auto originalPalette = qApp->palette();
  for (const QString theme : {QStringLiteral("light"), QStringLiteral("dark")}) {
    auto palette = qApp->style()->standardPalette();
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
    Session session;
    // Render a centered landscape-to-portrait pair without changing the
    // geometry or marker assumptions used by the functional tests above.
    connect(
        &session.controller, &ScreenCalibrationController::sendMessage, &session.controller,
        [&session](const QJsonObject &message) {
          if (message.value("type").toString() != "show")
            return;
          const auto target = message.value("target").toString();
          const bool first = target == "Windows";
          const QJsonArray points = first ? QJsonArray{0.15, 0.85, 0.5} : QJsonArray{0.3, 0.7, 0.5};
          auto reply = response(message, "ready", points, ++session.sequences[target]);
          reply.insert(
              "screen",
              QJsonObject{
                  {"width", first ? 2560 : 1440},
                  {"height", first ? 1440 : 2560},
                  {"dpr", first ? 1 : 2},
                  {"screenCount", 1}
              }
          );
          QTimer::singleShot(0, &session.controller, [&session, reply] { session.controller.receiveMessage(reply); });
        }
    );
    session.controller.setTransportAvailable(true);
    ScreenCalibrationDialog dialog(nullptr, session.controller, alignment());
    dialog.show();
    QTRY_VERIFY(control<QPushButton>(dialog, "useCalibration")->isEnabled());
    QVERIFY(dialog.grab().save(destination + "/calibration-" + theme + ".png"));
    dialog.resize(560, 420);
    QCoreApplication::processEvents();
    QVERIFY(dialog.grab().save(destination + "/calibration-" + theme + "-compact.png"));
    session.controller.receiveMessage(response(session.show("Mac"), "points", {0.3, 0.31, 0.305}, 100));
    QCoreApplication::processEvents();
    QVERIFY(dialog.grab().save(destination + "/calibration-" + theme + "-invalid.png"));
  }
  qApp->setPalette(originalPalette);
  if (!qm.isEmpty())
    qApp->removeTranslator(&translator);
  if (!qtQm.isEmpty())
    qApp->removeTranslator(&qtTranslator);
}

QTEST_MAIN(ScreenCalibrationTests)
