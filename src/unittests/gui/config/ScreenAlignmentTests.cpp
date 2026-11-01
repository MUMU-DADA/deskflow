/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenAlignmentTests.h"

#include "common/Settings.h"
#include "gui/config/ScreenAlignment.h"
#include "gui/config/ServerConfig.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMap>
#include <QTemporaryDir>
#include <QTextStream>

#include <cmath>
#include <limits>

namespace {
ScreenAlignment sample()
{
  return {"Windows", "Mac", Qt::Horizontal, {600, 300}, {300, 600}, -120, true};
}

ScreenEdgeCalibration calibrationSample()
{
  return {{0.25, 0.3125}, {0.75, 0.5625}, 16.0 / 9.0, 9.0 / 16.0};
}

void seedComputers(const QMap<int, QString> &names = {{6, "Windows"}, {7, "Mac"}})
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
  settings.sync();
}

QString exported(const ServerConfig &config)
{
  QString text;
  QTextStream stream(&text);
  stream << config;
  return text;
}

int storedAlignments()
{
  auto &settings = Settings::proxy();
  settings.beginGroup("internalConfig");
  const int count = settings.beginReadArray("screenAlignments");
  settings.endArray();
  settings.endGroup();
  return count;
}
} // namespace

void ScreenAlignmentTests::init()
{
  Settings::proxy().clear();
  Settings::proxy().sync();
  Settings::clearState();
  Settings::setValue(Settings::Core::ComputerName, "Windows");
  Settings::setValue(Settings::Server::GridWidth, 5);
  Settings::setValue(Settings::Server::GridHeight, 3);
  seedComputers();
}

void ScreenAlignmentTests::geometry_data()
{
  QTest::addColumn<QSizeF>("first");
  QTest::addColumn<QSizeF>("second");
  QTest::addColumn<int>("orientation");
  QTest::addColumn<double>("offset");
  QTest::addColumn<double>("firstStart");
  QTest::addColumn<double>("firstEnd");
  QTest::addColumn<double>("secondStart");
  QTest::addColumn<double>("secondEnd");

  QTest::newRow("equal") << QSizeF(600, 300) << QSizeF(600, 300) << int(Qt::Horizontal) << 0.0 << 0.0 << 100.0 << 0.0
                         << 100.0;
  QTest::newRow("centered-portrait") << QSizeF(600, 300) << QSizeF(300, 600) << int(Qt::Horizontal) << -150.0 << 0.0
                                     << 100.0 << 25.0 << 75.0;
  QTest::newRow("offset-portrait") << QSizeF(600, 300) << QSizeF(300, 600) << int(Qt::Horizontal) << -120.0 << 0.0
                                   << 100.0 << 20.0 << 70.0;
  QTest::newRow("positive-offset") << QSizeF(600, 300) << QSizeF(300, 600) << int(Qt::Horizontal) << 50.0 << (100.0 / 6)
                                   << 100.0 << 0.0 << (250.0 / 6);
  QTest::newRow("vertical-centered") << QSizeF(600, 300) << QSizeF(300, 600) << int(Qt::Vertical) << 150.0 << 25.0
                                     << 75.0 << 0.0 << 100.0;
  QTest::newRow("vertical-offset") << QSizeF(300, 600) << QSizeF(600, 300) << int(Qt::Vertical) << -120.0 << 0.0
                                   << 100.0 << 20.0 << 70.0;
  QTest::newRow("decimal-contained-endpoint") << QSizeF(600, 664.5) << QSizeF(300, 61.6) << int(Qt::Horizontal) << 447.8
                                              << (447.8 / 664.5 * 100) << (509.4 / 664.5 * 100) << 0.0 << 100.0;
}

void ScreenAlignmentTests::geometry()
{
  QFETCH(QSizeF, first);
  QFETCH(QSizeF, second);
  QFETCH(int, orientation);
  QFETCH(double, offset);
  QFETCH(double, firstStart);
  QFETCH(double, firstEnd);
  QFETCH(double, secondStart);
  QFETCH(double, secondEnd);
  auto alignment = sample();
  alignment.firstSizeMm = first;
  alignment.secondSizeMm = second;
  alignment.orientation = static_cast<Qt::Orientation>(orientation);
  alignment.offsetMm = offset;
  const auto intervals = alignment.intervals();
  QVERIFY(intervals);
  QVERIFY(intervals->hasOverlap);
  QVERIFY(std::abs(intervals->first.startPercent - firstStart) < 1e-10);
  QVERIFY(std::abs(intervals->first.endPercent - firstEnd) < 1e-10);
  QVERIFY(std::abs(intervals->second.startPercent - secondStart) < 1e-10);
  QVERIFY(std::abs(intervals->second.endPercent - secondEnd) < 1e-10);
}

void ScreenAlignmentTests::noOverlap_data()
{
  QTest::addColumn<double>("offset");
  QTest::newRow("touch-bottom") << 300.0;
  QTest::newRow("touch-top") << -600.0;
  QTest::newRow("below") << 500.0;
  QTest::newRow("above") << -900.0;
}

void ScreenAlignmentTests::noOverlap()
{
  QFETCH(double, offset);
  auto alignment = sample();
  alignment.offsetMm = offset;
  const auto intervals = alignment.intervals();
  QVERIFY(intervals);
  QVERIFY(!intervals->hasOverlap);
}

void ScreenAlignmentTests::invalidGeometry()
{
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double infinity = std::numeric_limits<double>::infinity();
  for (double value : {0.0, -1.0, nan, infinity}) {
    for (int component = 0; component < 4; ++component) {
      auto alignment = sample();
      if (component == 0)
        alignment.firstSizeMm.setWidth(value);
      if (component == 1)
        alignment.firstSizeMm.setHeight(value);
      if (component == 2)
        alignment.secondSizeMm.setWidth(value);
      if (component == 3)
        alignment.secondSizeMm.setHeight(value);
      QVERIFY(!alignment.isValid());
      QVERIFY(!alignment.intervals());
    }
  }
  for (double value : {nan, infinity, -infinity}) {
    auto alignment = sample();
    alignment.offsetMm = value;
    QVERIFY(!alignment.intervals());
  }
  auto alignment = sample();
  alignment.firstName.clear();
  QVERIFY(!alignment.intervals());
  alignment = sample();
  alignment.secondName = "WINDOWS";
  QVERIFY(!alignment.intervals());
  alignment = sample();
  alignment.orientation = static_cast<Qt::Orientation>(0);
  QVERIFY(!alignment.intervals());
  alignment = sample();
  alignment.secondSizeMm.setHeight(std::numeric_limits<double>::max());
  alignment.offsetMm = std::numeric_limits<double>::max();
  QVERIFY(!alignment.intervals());
  alignment = sample();
  alignment.firstSizeMm.setHeight(1e10);
  alignment.secondSizeMm.setHeight(1);
  alignment.offsetMm = 1e10 - 1;
  QVERIFY(!alignment.intervals()); // Normalized endpoints collapse to the same core float.
}

void ScreenAlignmentTests::defaultLinksAndCopy()
{
  ServerConfig config;
  const auto legacy = exported(config);
  QVERIFY(legacy.contains("right = Mac\n"));
  QVERIFY(legacy.contains("left = Windows\n"));
  QVERIFY(!legacy.contains("right("));
  QCOMPARE(config.adjacentPairs().size(), 1);
  QVERIFY(!config.screenAlignment(sample().pair()));
  ServerConfig copy = config;
  QVERIFY(copy == config);
  QVERIFY(copy.setScreenAlignment(sample()));
  QVERIFY(!(copy == config));
  QVERIFY(!config.screenAlignment(sample().pair()));
  auto disabled = sample();
  disabled.enabled = false;
  QVERIFY(copy.setScreenAlignment(disabled));
  QCOMPARE(exported(copy), legacy);
  QVERIFY(copy.clearScreenAlignment(disabled.pair()));
  QVERIFY(!copy.clearScreenAlignment(disabled.pair()));
  QVERIFY(copy == config);
  auto wrong = sample();
  wrong.secondName = "Other";
  QVERIFY(!config.setScreenAlignment(wrong));
  wrong = sample();
  wrong.orientation = Qt::Vertical;
  QVERIFY(!config.setScreenAlignment(wrong));
}

void ScreenAlignmentTests::reciprocalExport()
{
  ServerConfig config;
  QVERIFY(config.setScreenAlignment(sample()));
  const auto text = exported(config);
  QVERIFY(text.contains("right(0,100) = Mac(20,70)\n"));
  QVERIFY(text.contains("left(20,70) = Windows(0,100)\n"));
  QVERIFY(!text.contains("right = Mac"));
  QVERIFY(!text.contains("left = Windows"));
}

void ScreenAlignmentTests::verticalExport()
{
  seedComputers({{6, "Windows"}, {11, "Mac"}});
  ServerConfig config;
  auto alignment = sample();
  alignment.orientation = Qt::Vertical;
  alignment.offsetMm = 150;
  QVERIFY(config.setScreenAlignment(alignment));
  const auto text = exported(config);
  QVERIFY(text.contains("down(25,75) = Mac(0,100)\n"));
  QVERIFY(text.contains("up(0,100) = Windows(25,75)\n"));
}

void ScreenAlignmentTests::disjointExport()
{
  ServerConfig config;
  auto alignment = sample();
  alignment.offsetMm = 500;
  QVERIFY(config.setScreenAlignment(alignment));
  const auto text = exported(config);
  QVERIFY(!text.contains("right"));
  QVERIFY(!text.contains("left"));
  QVERIFY(text.contains("Windows:\n"));
  QVERIFY(text.contains("Mac:\n"));
}

void ScreenAlignmentTests::fractionalPersistenceAndLocale()
{
  ServerConfig config;
  auto alignment = sample();
  alignment.firstSizeMm = {600.1234567890123, 300.9876543210123};
  alignment.secondSizeMm = {300.2345678901234, 600.8765432101234};
  alignment.offsetMm = -123.4567890123456;
  QVERIFY(config.setScreenAlignment(alignment));
  const auto before = exported(config);
  config.commit();
  Settings::proxy().sync();
  Settings::save(false);
  ServerConfig loaded;
  QVERIFY(loaded == config);
  const auto recalled = loaded.screenAlignment(alignment.pair());
  QVERIFY(recalled);
  QCOMPARE(recalled->firstSizeMm.width(), alignment.firstSizeMm.width());
  QCOMPARE(recalled->firstSizeMm.height(), alignment.firstSizeMm.height());
  QCOMPARE(recalled->offsetMm, alignment.offsetMm);
  QCOMPARE(exported(loaded), before);
  QCOMPARE(storedAlignments(), 1);
  const auto previousLocale = QLocale();
  QLocale::setDefault(QLocale(QLocale::German));
  const auto localized = exported(loaded);
  QLocale::setDefault(previousLocale);
  QCOMPARE(localized, before);
  QVERIFY(before.contains('.'));
}

void ScreenAlignmentTests::topologyChanges_data()
{
  QTest::addColumn<int>("winIndex");
  QTest::addColumn<int>("macIndex");
  QTest::addColumn<QString>("secondName");
  QTest::newRow("reverse") << 7 << 6 << QString("Mac");
  QTest::newRow("orientation") << 6 << 11 << QString("Mac");
  QTest::newRow("gap") << 6 << 8 << QString("Mac");
  QTest::newRow("delete") << 6 << -1 << QString("Mac");
  QTest::newRow("different-neighbor") << 6 << 7 << QString("Other");
}

void ScreenAlignmentTests::topologyChanges()
{
  QFETCH(int, winIndex);
  QFETCH(int, macIndex);
  QFETCH(QString, secondName);
  ServerConfig original;
  QVERIFY(original.setScreenAlignment(sample()));
  original.commit();
  QMap<int, QString> names{{winIndex, "Windows"}};
  if (macIndex >= 0)
    names.insert(macIndex, secondName);
  seedComputers(names); // Simulate a persisted move, replacement, or removal.
  ServerConfig changed;
  QVERIFY(!changed.screenAlignment(sample().pair()));
  QVERIFY(!exported(changed).contains("(20,70)"));
  changed.commit();
  QCOMPARE(storedAlignments(), 0);
  seedComputers();
  ServerConfig restoredLayout;
  QVERIFY(!restoredLayout.screenAlignment(sample().pair())); // Deleted calibration cannot revive on cell reuse.
}

void ScreenAlignmentTests::movedPairKeepsCalibration()
{
  ServerConfig original;
  QVERIFY(original.setScreenAlignment(sample()));
  original.commit();
  seedComputers({{0, "Windows"}, {1, "Mac"}});
  ServerConfig shifted;
  QVERIFY(shifted.screenAlignment(sample().pair()));
  QCOMPARE(exported(shifted), exported(original));
}

void ScreenAlignmentTests::serverRenameClearsCalibration()
{
  ServerConfig config;
  QVERIFY(config.setScreenAlignment(sample()));
  Settings::setValue(Settings::Core::ComputerName, "Renamed");
  config.updateServerName();
  QVERIFY(!config.screenAlignment(sample().pair()));
  QVERIFY(exported(config).contains("left = Renamed\n"));
  config.commit();
  QCOMPARE(storedAlignments(), 0);
}

void ScreenAlignmentTests::malformedMetadataIgnored()
{
  ServerConfig config;
  QVERIFY(config.setScreenAlignment(sample()));
  config.commit();
  Settings::proxy().setValue("internalConfig/screenAlignments/1/offsetMm", "nan");
  ServerConfig malformed;
  QVERIFY(!malformed.screenAlignment(sample().pair()));
  QVERIFY(exported(malformed).contains("right = Mac\n"));
  QVERIFY(!exported(malformed).contains("right("));
  malformed.commit();
  QCOMPARE(storedAlignments(), 0);
}

void ScreenAlignmentTests::calibrationTransform_data()
{
  QTest::addColumn<double>("firstA");
  QTest::addColumn<double>("secondA");
  QTest::addColumn<double>("firstB");
  QTest::addColumn<double>("secondB");
  QTest::addColumn<double>("ratio");
  QTest::addColumn<double>("offset");
  QTest::newRow("equal") << 0.2 << 0.2 << 0.8 << 0.8 << 1.0 << 0.0;
  QTest::newRow("taller") << 0.2 << 0.3 << 0.8 << 0.6 << 2.0 << -0.4;
  QTest::newRow("shorter") << 0.3 << 0.2 << 0.6 << 0.8 << 0.5 << 0.2;
  QTest::newRow("positive-offset") << 0.3 << 0.1 << 0.9 << 0.4 << 2.0 << 0.1;
  QTest::newRow("both-colors-reversed") << 0.8 << 0.6 << 0.2 << 0.3 << 2.0 << -0.4;
  QTest::newRow("decimal-minimum-spacing") << 0.2 << 0.2 << 0.3 << 0.3 << 1.0 << 0.0;
  QTest::newRow("boundary-points") << 0.0 << 0.0 << 1.0 << 1.0 << 1.0 << 0.0;
  QTest::newRow("fractional-fit") << 0.2875 << 0.25 << 0.9875 << 0.65 << 1.75 << -0.15;
}

void ScreenAlignmentTests::calibrationTransform()
{
  QFETCH(double, firstA);
  QFETCH(double, secondA);
  QFETCH(double, firstB);
  QFETCH(double, secondB);
  QFETCH(double, ratio);
  QFETCH(double, offset);
  ScreenEdgeCalibration calibration{{firstA, secondA}, {firstB, secondB}};
  QCOMPARE(calibration.error(), ScreenEdgeCalibration::Error::None);
  const auto fitted = calibration.transform();
  QVERIFY(fitted);
  QVERIFY(std::abs(fitted->ratio - ratio) < 1e-12);
  QVERIFY(std::abs(fitted->offset - offset) < 1e-12);
  QVERIFY(std::abs(*calibration.validationResidual(calibration.a)) < 1e-12);
  QVERIFY(std::abs(*calibration.validationResidual(calibration.b)) < 1e-12);

  calibration.firstAspectRatio = 0.5;
  calibration.secondAspectRatio = 3;
  QVERIFY(calibration.transform() == fitted); // Preview shape cannot change edge calibration.
}

void ScreenAlignmentTests::calibrationValidation()
{
  using Error = ScreenEdgeCalibration::Error;
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double infinity = std::numeric_limits<double>::infinity();
  for (double invalid : {nan, infinity, -infinity, -0.001, 1.001}) {
    for (int component = 0; component < 4; ++component) {
      auto calibration = calibrationSample();
      double *positions[] = {&calibration.a.first, &calibration.a.second, &calibration.b.first, &calibration.b.second};
      *positions[component] = invalid;
      QCOMPARE(calibration.error(), std::isfinite(invalid) ? Error::OutOfRange : Error::NonFinite);
      QVERIFY(!calibration.transform());
    }
  }
  for (bool first : {false, true}) {
    for (double distance : {0.0, 0.099, 0.1 - 1e-10}) {
      auto calibration = calibrationSample();
      if (first)
        calibration.b.first = calibration.a.first + distance;
      else
        calibration.b.second = calibration.a.second + distance;
      QCOMPARE(calibration.error(), Error::PointsTooClose);
      QVERIFY(!calibration.transform());
    }
  }
  auto calibration = calibrationSample();
  std::swap(calibration.a.second, calibration.b.second);
  QCOMPARE(calibration.error(), Error::Reversed);
  QVERIFY(!calibration.transform());
  for (double aspect : {0.0, -1.0, nan, infinity, std::numeric_limits<double>::denorm_min()}) {
    for (bool first : {false, true}) {
      calibration = calibrationSample();
      if (first)
        calibration.firstAspectRatio = aspect;
      else
        calibration.secondAspectRatio = aspect;
      QCOMPARE(calibration.error(), Error::InvalidAspectRatio);
      QVERIFY(!calibration.transform());
    }
  }
}

void ScreenAlignmentTests::calibrationThirdPoint()
{
  const auto calibration = calibrationSample(); // first = -0.375 + 2 * second
  const auto original = calibration.transform();
  QCOMPARE(*calibration.validationResidual({0.5, 0.4375}), 0.0);
  QVERIFY(std::abs(*calibration.validationResidual({0.52, 0.4375}) - 0.02) < 1e-12);
  QVERIFY(std::abs(*calibration.validationResidual({0.48, 0.4375}) + 0.02) < 1e-12);
  QVERIFY(!calibration.validationResidual({-0.1, 0.4}));
  QVERIFY(!calibration.validationResidual({0.4, 1.1}));
  QVERIFY(!calibration.validationResidual({std::numeric_limits<double>::quiet_NaN(), 0.4}));
  QVERIFY(calibration.transform() == original); // Validation never refits the two anchors.
}

void ScreenAlignmentTests::relativeGeometryAndIntervals()
{
  auto alignment = sample();
  const auto manual = alignment.visualGeometry();
  QVERIFY(manual);
  QVERIFY(!manual->relative);
  QCOMPARE(manual->firstSize, alignment.firstSizeMm);
  QCOMPARE(manual->offset, alignment.offsetMm);

  alignment.calibration = calibrationSample();
  alignment.firstSizeMm = {};
  alignment.secondSizeMm = {};
  alignment.offsetMm = std::numeric_limits<double>::quiet_NaN();
  QVERIFY(alignment.isValid()); // Unmeasured manual dimensions are irrelevant in relative mode.
  const auto horizontal = alignment.visualGeometry();
  QVERIFY(horizontal);
  QVERIFY(horizontal->relative);
  QCOMPARE(horizontal->firstSize, QSizeF(16.0 / 9.0, 1));
  QCOMPARE(horizontal->secondSize, QSizeF(2 * 9.0 / 16.0, 2));
  QCOMPARE(horizontal->offset, -0.375);
  const auto intervals = alignment.intervals();
  QVERIFY(intervals);
  QVERIFY(intervals->hasOverlap);
  QCOMPARE(intervals->first.startPercent, 0.0);
  QCOMPARE(intervals->first.endPercent, 100.0);
  QCOMPARE(intervals->second.startPercent, 18.75);
  QCOMPARE(intervals->second.endPercent, 68.75);

  alignment.orientation = Qt::Vertical;
  const auto vertical = alignment.visualGeometry();
  QVERIFY(vertical);
  QVERIFY(vertical->relative);
  QCOMPARE(vertical->firstSize, QSizeF(1, 9.0 / 16.0));
  QCOMPARE(vertical->secondSize, QSizeF(2, 2 * 16.0 / 9.0));
  QVERIFY(alignment.intervals() == intervals);

  alignment.calibration->b.first = alignment.calibration->a.first;
  QVERIFY(!alignment.isValid());
  QVERIFY(!alignment.visualGeometry());
  QVERIFY(!alignment.intervals());
}

void ScreenAlignmentTests::calibrationPersistence()
{
  seedComputers({{6, "Windows"}, {7, "Mac"}, {8, "Linux"}});
  ServerConfig config;
  auto calibrated = sample();
  calibrated.calibration = calibrationSample();
  calibrated.calibration->firstAspectRatio = 1.7777777777771234;
  calibrated.calibration->secondAspectRatio = 0.5625000000001234;
  QVERIFY(config.setScreenAlignment(calibrated));
  auto manual = sample();
  manual.firstName = "Mac";
  manual.secondName = "Linux";
  QVERIFY(config.setScreenAlignment(manual));
  const auto before = exported(config);
  QVERIFY(before.contains("right(0,100) = Mac(18.75,68.75)\n"));
  QVERIFY(before.contains("left(18.75,68.75) = Windows(0,100)\n"));
  calibrated.calibration->a.first = 0.2345678901234567;
  calibrated.calibration->b.first = 0.7654321098765432;
  QVERIFY(config.setScreenAlignment(calibrated));
  const auto persistedExport = exported(config);
  config.commit();
  Settings::proxy().sync();
  Settings::save(false);
  const auto prefix = QStringLiteral("internalConfig/screenAlignments/");
  QCOMPARE(Settings::proxy().value(prefix + "1/version").toInt(), 2);
  QCOMPARE(Settings::proxy().value(prefix + "1/method").toString(), "two-point");
  for (const auto &key : {"firstWidthMm", "firstHeightMm", "secondWidthMm", "secondHeightMm", "offsetMm"})
    QVERIFY(!Settings::proxy().contains(prefix + "1/" + key));
  QCOMPARE(Settings::proxy().value(prefix + "2/version").toInt(), 1);
  QVERIFY(Settings::proxy().contains(prefix + "2/firstWidthMm"));

  ServerConfig loaded;
  QVERIFY(loaded == config);
  const auto recalled = loaded.screenAlignment(calibrated.pair());
  QVERIFY(recalled);
  QVERIFY(recalled->calibration == calibrated.calibration);
  QVERIFY(recalled->firstSizeMm.isEmpty()); // No invented physical dimensions on load.
  QVERIFY(recalled->secondSizeMm.isEmpty());
  QVERIFY(!loaded.screenAlignment(manual.pair())->calibration);
  QCOMPARE(exported(loaded), persistedExport);
  QCOMPARE(storedAlignments(), 2);
}

void ScreenAlignmentTests::malformedCalibrationMetadata_data()
{
  QTest::addColumn<QString>("key");
  QTest::addColumn<QString>("value");
  QTest::newRow("non-finite") << "firstA" << "nan";
  QTest::newRow("outside-edge") << "secondA" << "1.1";
  QTest::newRow("points-too-close") << "firstB" << "0.26";
  QTest::newRow("reversed") << "secondB" << "0.1";
  QTest::newRow("invalid-aspect") << "firstAspectRatio" << "0";
  QTest::newRow("unknown-method") << "method" << "future-method";
  QTest::newRow("missing-coordinate") << "secondB" << "";
}

void ScreenAlignmentTests::malformedCalibrationMetadata()
{
  QFETCH(QString, key);
  QFETCH(QString, value);
  ServerConfig config;
  auto calibrated = sample();
  calibrated.calibration = calibrationSample();
  QVERIFY(config.setScreenAlignment(calibrated));
  config.commit();
  const auto path = "internalConfig/screenAlignments/1/" + key;
  if (value.isEmpty())
    Settings::proxy().remove(path);
  else
    Settings::proxy().setValue(path, value);
  ServerConfig loaded;
  QVERIFY(!loaded.screenAlignment(calibrated.pair()));
  QVERIFY(exported(loaded).contains("right = Mac\n"));
  loaded.commit();
  QCOMPARE(storedAlignments(), 0);
}

void ScreenAlignmentTests::manualReplacesCalibration()
{
  ServerConfig config;
  auto alignment = sample();
  alignment.calibration = calibrationSample();
  QVERIFY(config.setScreenAlignment(alignment));
  config.commit();
  QVERIFY(config.setScreenAlignment(sample()));
  config.commit();
  const auto prefix = QStringLiteral("internalConfig/screenAlignments/1/");
  QCOMPARE(Settings::proxy().value(prefix + "version").toInt(), 1);
  QVERIFY(!Settings::proxy().contains(prefix + "firstA"));
  QVERIFY(!Settings::proxy().contains(prefix + "method"));
  ServerConfig loaded;
  QVERIFY(loaded == config);
  QVERIFY(!loaded.screenAlignment(sample().pair())->calibration);
  QVERIFY(exported(loaded).contains("right(0,100) = Mac(20,70)\n"));
}

int main(int argc, char **argv)
{
  QApplication application(argc, argv);
  QTemporaryDir isolated;
  if (!isolated.isValid())
    return 2;
  const QString configRoot = isolated.path() + "/config";
  const QString stateRoot = isolated.path() + "/state";
  QDir().mkpath(configRoot + "/Deskflow");
  QDir().mkpath(stateRoot);
  qputenv("XDG_CONFIG_HOME", configRoot.toUtf8());
  qputenv("XDG_STATE_HOME", stateRoot.toUtf8());

  // Settings::setSettingsFile initializes Settings first. Seed its initial
  // Windows portable location before that can load/upgrade the real profile.
  // CMake gives this test its own executable directory, separate from other tests.
#ifdef Q_OS_WIN
  const QString bootstrap = QCoreApplication::applicationDirPath() + "/settings/Deskflow.conf";
  QDir().mkpath(QFileInfo(bootstrap).absolutePath());
  QFile portable(bootstrap);
  if (!portable.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    return 2; // Never overwrite an existing profile, even in the test directory.
  portable.close();
#endif
  const QString config = configRoot + "/Deskflow/Deskflow.conf";
  QFile configFile(config);
  if (!configFile.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    return 2;
  configFile.close();
  Settings::setSettingsFile(config);
  Settings::setStateFile(stateRoot + "/Deskflow.state");
  ScreenAlignmentTests tests;
  const int result = QTest::qExec(&tests, argc, argv);
  Settings::proxy().sync();
  Settings::save(false);
#ifdef Q_OS_WIN
  QFile::remove(bootstrap);
#endif
  return result;
}
