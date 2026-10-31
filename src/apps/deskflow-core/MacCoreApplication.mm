/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MacCoreApplication.h"

#include <QByteArray>
#include <QDebug>

#import <AppKit/AppKit.h>

namespace deskflow {

namespace {
constexpr auto foregroundTransformVariable = "QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM";
}

MacCoreApplicationBootstrap::MacCoreApplicationBootstrap()
    : m_previousValue(qgetenv(foregroundTransformVariable)),
      m_previousWasSet(qEnvironmentVariableIsSet(foregroundTransformVariable))
{
}

MacCoreApplicationBootstrap::~MacCoreApplicationBootstrap()
{
  restoreEnvironment();
}

bool MacCoreApplicationBootstrap::prepare()
{
  // Qt's Cocoa plugin otherwise promotes this executable to Regular while
  // constructing QApplication, before any later attempt to hide the core.
  // This is a private Qt implementation switch, verified against Qt 6.9.3's
  // qcocoaintegration.mm. MacCoreApplicationTests checks the native policy
  // before our explicit policy change so a Qt upgrade cannot silently break it.
  if (![NSThread isMainThread] || !qputenv(foregroundTransformVariable, "1")) {
    qCritical("Unable to prepare the macOS core's background application policy");
    return false;
  }
  m_environmentChanged = true;
  return true;
}

bool MacCoreApplicationBootstrap::restoreEnvironment()
{
  if (!m_environmentChanged)
    return true;

  // Enter/exit commands inherit the core's environment. Do not alter the Qt
  // activation policy of unrelated applications started by those commands.
  if (!(m_previousWasSet ? qputenv(foregroundTransformVariable, m_previousValue)
                         : qunsetenv(foregroundTransformVariable))) {
    qCritical("Unable to restore the macOS core's Qt initialization environment");
    return false;
  }
  m_environmentChanged = false;
  return true;
}

bool MacCoreApplicationBootstrap::finish()
{
  const bool environmentRestored = restoreEnvironment();
  @autoreleasepool {
    if (![NSThread isMainThread] || !NSApp) {
      qCritical("The macOS core's background policy must be initialized on the main thread after QApplication");
      return false;
    }

    const auto policy = [NSApp activationPolicy];
    if (policy == NSApplicationActivationPolicyRegular) {
      qWarning(
          "The macOS core became a foreground application during Qt initialization; check the Qt Cocoa integration"
      );
    }

    // Prohibited corresponds to LSBackgroundOnly, matching the core's former
    // background-only Carbon transform. The core has no windows to activate;
    // QApplication and its event loop are still needed for platform services.
    if ((policy != NSApplicationActivationPolicyProhibited &&
         ![NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited]) ||
        [NSApp activationPolicy] != NSApplicationActivationPolicyProhibited) {
      qCritical("Unable to establish the macOS core's background-only application policy");
      return false;
    }
  }
  return environmentRestored;
}

} // namespace deskflow
