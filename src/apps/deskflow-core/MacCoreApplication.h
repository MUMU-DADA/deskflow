/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>

namespace deskflow {

// These methods run on the main thread, immediately around QApplication's
// construction. Do not create NSApplication before Qt creates QNSApplication.
class MacCoreApplicationBootstrap
{
public:
  MacCoreApplicationBootstrap();
  ~MacCoreApplicationBootstrap();
  MacCoreApplicationBootstrap(const MacCoreApplicationBootstrap &) = delete;
  MacCoreApplicationBootstrap &operator=(const MacCoreApplicationBootstrap &) = delete;

  bool prepare();
  bool finish();

private:
  bool restoreEnvironment();

  const QByteArray m_previousValue;
  const bool m_previousWasSet;
  bool m_environmentChanged = false;
};

} // namespace deskflow
