/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/ForegroundGame.h"

#include <QDir>
#include <QFileInfo>

namespace deskflow {
namespace {
QString normalizeIdentifier(QString value)
{
  value.replace('\\', '/');
  return QDir::cleanPath(value.trimmed()).toCaseFolded();
}
} // namespace

bool matchesGameApplication(const QStringList &identifiers, const QStringList &rules)
{
  for (const auto &rule : rules) {
    if (rule.trimmed().isEmpty())
      continue;
    const auto normalizedRule = normalizeIdentifier(rule);
    for (const auto &identifier : identifiers) {
      if (!identifier.isEmpty() && normalizeIdentifier(identifier) == normalizedRule)
        return true;
    }
  }
  return false;
}

bool isKnownGamePath(const QString &path)
{
  const auto normalized = normalizeIdentifier(path);
  // A library location is a fallback heuristic, not proof that an application is
  // a game. Known non-game software is excluded; native category data takes
  // precedence at the call site. Unrecognised games can use an explicit rule.
  static const QStringList nonGames = {
      "blender",
      "blender.exe",
      "chrome",
      "chrome.exe",
      "msedge.exe",
      "firefox",
      "firefox.exe",
      "safari",
      "vlc",
      "vlc.exe",
      "mpv",
      "mpv.exe",
      "obs",
      "obs64.exe",
      "wallpaper32.exe",
      "wallpaper64.exe",
      "losslessscaling.exe",
      "steam",
      "steam.exe",
      "steamwebhelper",
      "steamwebhelper.exe"
  };
  if (nonGames.contains(QFileInfo(normalized).fileName()))
    return false;
  return normalized.contains(QStringLiteral("/steamapps/common/"));
}

bool coversGameDisplay(const QRectF &window, const QRectF &display, qreal tolerance)
{
  if (!window.isValid() || !display.isValid() || tolerance < 0)
    return false;
  return qAbs(window.left() - display.left()) <= tolerance && qAbs(window.top() - display.top()) <= tolerance &&
         qAbs(window.right() - display.right()) <= tolerance && qAbs(window.bottom() - display.bottom()) <= tolerance;
}

} // namespace deskflow
