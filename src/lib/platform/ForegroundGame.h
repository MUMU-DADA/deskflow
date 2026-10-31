/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QRectF>
#include <QStringList>

namespace deskflow {

struct ForegroundGameState
{
  bool supported = false;
  bool fullscreen = false;
  bool game = false;
  QString application;
  QString detail;
};

// Inspect only this computer's foreground application. The caller decides whether
// this computer is currently receiving shared input and whether the feature is enabled.
ForegroundGameState queryForegroundGame(const QStringList &gameApps, const QString &stateFile);

// Exact application identifiers, executable names or paths, never window titles.
bool matchesGameApplication(const QStringList &identifiers, const QStringList &rules);
bool isKnownGamePath(const QString &path);
bool coversGameDisplay(const QRectF &window, const QRectF &display, qreal tolerance = 1.0);

} // namespace deskflow
