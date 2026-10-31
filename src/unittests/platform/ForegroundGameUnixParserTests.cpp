/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/ForegroundGameUnixParser.h"
#include "platform/ForegroundGame.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QTemporaryFile>
#include <QtTest>

using namespace deskflow::foregroundGameUnix;

class ForegroundGameUnixParserTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void focusedWindowWinsOverBackgroundFullscreen()
  {
    const auto tree = QJsonDocument::fromJson(R"({"nodes":[
      {"app_id":"background-game","fullscreen_mode":1,"focused":false},
      {"app_id":"browser","fullscreen_mode":0,"focused":true,"visible":true}
    ]})").object();
    const auto info = swayWindow(tree);
    QCOMPARE(info.identifiers.value(0), QStringLiteral("browser"));
    QVERIFY(!info.fullscreen);
  }

  void focusedFloatingFullscreenGame()
  {
    const auto tree = QJsonDocument::fromJson(R"({"floating_nodes":[
      {"app_id":"org.example.Game","pid":123,"fullscreen_mode":1,"focused":true,"visible":true}
    ]})").object();
    const auto info = swayWindow(tree);
    QVERIFY(info.fullscreen);
    QCOMPARE(info.pid, qint64(123));
  }

  void borderlessMustFillItsOutput()
  {
    auto tree = QJsonDocument::fromJson(R"({"type":"output","rect":{"x":1920,"y":0,"width":1920,"height":1080},
      "nodes":[{"app_id":"game","focused":true,"border":"none","fullscreen_mode":0,
                "rect":{"x":1920,"y":0,"width":1920,"height":1080}}]})").object();
    QVERIFY(swayWindow(tree).fullscreen);
    auto node = tree["nodes"].toArray()[0].toObject();
    node["rect"] = QJsonObject{{"x", 1920}, {"y", 30}, {"width", 1920}, {"height", 1050}};
    tree["nodes"] = QJsonArray{node};
    QVERIFY(!swayWindow(tree).fullscreen);
    node["border"] = "normal";
    node["rect"] = tree["rect"];
    tree["nodes"] = QJsonArray{node};
    QVERIFY(!swayWindow(tree).fullscreen);
  }

  void hiddenSwayWindowIsIgnored()
  {
    const auto node = QJsonObject{{"app_id", "game"}, {"focused", true}, {"visible", false}, {"fullscreen_mode", 1}};
    QVERIFY(swayWindow(node).identifiers.isEmpty());
  }

  void hyprlandUsesEffectiveState()
  {
    QJsonObject window{{"class", "game"}, {"mapped", true}, {"fullscreen", 0}, {"fullscreenClient", 2}};
    QVERIFY(!hyprlandWindow(window).fullscreen);
    window["fullscreen"] = 1; // Maximized is not fullscreen.
    QVERIFY(!hyprlandWindow(window).fullscreen);
    window["fullscreen"] = 2;
    QVERIFY(hyprlandWindow(window).fullscreen);
    window["fullscreen"] = 3;
    QVERIFY(hyprlandWindow(window).fullscreen);
    window["hidden"] = true;
    QVERIFY(!hyprlandWindow(window).fullscreen);
  }

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
  void adapterRejectsStaleAndMalformedState()
  {
    QTemporaryFile file;
    QVERIFY(file.open());
    QJsonObject state{{"version", 1}, {"updatedAtMs", QDateTime::currentMSecsSinceEpoch()},
                      {"application", "org.example.Game"}, {"fullscreen", true}};
    const auto write = [&]() {
      file.resize(0);
      file.seek(0);
      file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
      file.flush();
    };
    write();
    auto result = deskflow::queryForegroundGame({"org.example.Game"}, file.fileName());
    QVERIFY(result.supported && result.fullscreen && result.game);
    state["updatedAtMs"] = QDateTime::currentMSecsSinceEpoch() - 5000;
    write();
    QVERIFY(!deskflow::queryForegroundGame({"org.example.Game"}, file.fileName()).supported);
    state["updatedAtMs"] = QDateTime::currentMSecsSinceEpoch();
    state["fullscreen"] = "true"; // JSON strings must not become booleans.
    write();
    QVERIFY(!deskflow::queryForegroundGame({"org.example.Game"}, file.fileName()).supported);
    state["fullscreen"] = true;
    state["application"] = "";
    write();
    QVERIFY(!deskflow::queryForegroundGame({"org.example.Game"}, file.fileName()).fullscreen);
  }
#endif
};

QTEST_GUILESS_MAIN(ForegroundGameUnixParserTests)
#include "ForegroundGameUnixParserTests.moc"
