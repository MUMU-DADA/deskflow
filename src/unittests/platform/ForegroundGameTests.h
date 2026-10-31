/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

namespace deskflow {

class ForegroundGameTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void applicationRulesRequireExactIdentifiers();
  void applicationRulesNormalizePaths();
  void steamHeuristicExcludesKnownNonGames();
  void geometry_data();
  void geometry();
};

} // namespace deskflow
