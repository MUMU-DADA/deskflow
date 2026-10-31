/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class MacCoreApplicationTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void qtInitializationStaysBackground_data();
  void qtInitializationStaysBackground();
  void ordinaryHostKeepsGuiPolicy();
  void actualCoreRejectsGuiMetadata();
  void actualCoreEarlyExit_data();
  void actualCoreEarlyExit();
};
