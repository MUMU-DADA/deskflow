/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ForegroundGameTests.h"

#include "platform/ForegroundGame.h"

namespace deskflow {

void ForegroundGameTests::applicationRulesRequireExactIdentifiers()
{
  const QStringList ids{"/games/example/Game.exe", "Game.exe", "org.example.game"};
  QVERIFY(matchesGameApplication(ids, {"Game.exe"}));
  QVERIFY(matchesGameApplication(ids, {"org.example.game"}));
  QVERIFY(!matchesGameApplication(ids, {"example", "game", "*.exe", "*", ""}));
  QVERIFY(!matchesGameApplication({"Game.exe.launcher"}, {"Game.exe"}));
  QVERIFY(!matchesGameApplication({}, {"Game.exe"}));
  QVERIFY(!matchesGameApplication(ids, {}));
}

void ForegroundGameTests::applicationRulesNormalizePaths()
{
  QVERIFY(matchesGameApplication({"D:\\Games\\Example\\Game.exe"}, {" d:/games/example/GAME.exe "}));
  QVERIFY(!matchesGameApplication({"D:\\Games\\Example\\Game.exe"}, {"C:/Games/Example/Game.exe"}));
}

void ForegroundGameTests::steamHeuristicExcludesKnownNonGames()
{
  QVERIFY(isKnownGamePath("D:\\SteamLibrary\\steamapps\\common\\Example\\bin\\Game.exe"));
  QVERIFY(isKnownGamePath("/home/player/.steam/steam/steamapps/common/Example/game"));
  QVERIFY(!isKnownGamePath("/games/not-steamapps/common/Example/game"));
  QVERIFY(!isKnownGamePath("C:/Programs/Browser/chrome.exe"));
  QVERIFY(!isKnownGamePath("C:/Steam/steamapps/common/Blender/blender.exe"));
  QVERIFY(!isKnownGamePath("C:/Steam/steamapps/common/Wallpaper Engine/wallpaper64.exe"));
  QVERIFY(!isKnownGamePath("C:/Steam/steamapps/common/Example/chrome.exe"));
}

void ForegroundGameTests::geometry_data()
{
  QTest::addColumn<QRectF>("window");
  QTest::addColumn<QRectF>("display");
  QTest::addColumn<bool>("fullscreen");
  const QRectF display(0, 0, 1920, 1080);
  QTest::newRow("full-display") << display << display << true;
  QTest::newRow("secondary-monitor-negative-origin")
      << QRectF(-2560, -100, 2560, 1440) << QRectF(-2560, -100, 2560, 1440) << true;
  QTest::newRow("maximized-work-area") << QRectF(0, 0, 1920, 1040) << display << false;
  QTest::newRow("title-bar") << QRectF(0, 30, 1920, 1050) << display << false;
  QTest::newRow("same-size-on-another-monitor") << QRectF(1920, 0, 1920, 1080) << display << false;
  QTest::newRow("window-spans-displays") << QRectF(0, 0, 3840, 1080) << display << false;
  QTest::newRow("one-pixel-rounding") << QRectF(1, 0, 1919, 1080) << display << true;
  QTest::newRow("invalid-window") << QRectF() << display << false;
  QTest::newRow("invalid-display") << display << QRectF() << false;
}

void ForegroundGameTests::geometry()
{
  QFETCH(QRectF, window);
  QFETCH(QRectF, display);
  QFETCH(bool, fullscreen);
  QCOMPARE(coversGameDisplay(window, display), fullscreen);
}

} // namespace deskflow

QTEST_GUILESS_MAIN(deskflow::ForegroundGameTests)
