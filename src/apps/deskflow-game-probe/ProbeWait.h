/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QCoreApplication>
#include <QEventLoop>

#include <algorithm>
#include <chrono>
#include <thread>

#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace deskflow::probe {

inline void pumpMainEvents()
{
  QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
#ifdef Q_OS_MACOS
  // NSWorkspace's time-varying state advances through the main Cocoa run loop.
  // Pump the default mode (a member of the common modes set) without creating
  // NSApplication, presenting a window or changing application activation.
  CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.001, true);
#endif
}

template <typename Ready> bool waitUntil(std::chrono::steady_clock::time_point deadline, Ready ready)
{
  using namespace std::chrono;
  for (;;) {
    pumpMainEvents();
    if (ready())
      return true;
    const auto now = steady_clock::now();
    if (now >= deadline)
      return false;
    std::this_thread::sleep_for(std::min(deadline - now, duration_cast<steady_clock::duration>(milliseconds(5))));
  }
}

} // namespace deskflow::probe
