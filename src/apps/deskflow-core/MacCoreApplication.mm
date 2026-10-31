/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MacCoreApplication.h"

#include "common/Constants.h"

#include <QDebug>
#include <QString>

#import <AppKit/AppKit.h>

namespace deskflow {

namespace {
QString bundleString(CFBundleRef bundle, CFStringRef key)
{
  const auto value = CFBundleGetValueForInfoDictionaryKey(bundle, key);
  return value && CFGetTypeID(value) == CFStringGetTypeID() ? QString::fromCFString(static_cast<CFStringRef>(value))
                                                            : QString();
}

QString bundleString(NSBundle *bundle, NSString *key)
{
  id value = [bundle objectForInfoDictionaryKey:key];
  return [value isKindOfClass:[NSString class]] ? QString::fromNSString(value) : QString();
}
} // namespace

bool MacCoreApplicationBootstrap::prepare() const
{
  @autoreleasepool {
    if (![NSThread isMainThread] || NSApp) {
      qCritical("The macOS core's metadata must be checked on the main thread before QApplication");
      return false;
    }

    // A secondary executable's embedded __TEXT,__info_plist replaces the
    // enclosing GUI bundle's dictionary. Check both public bundle APIs without
    // creating NSApplication; Qt must create its own QNSApplication subclass.
    const auto expectedId = QStringLiteral("%1.core").arg(kRevFqdnName);
    const auto cfBundle = CFBundleGetMainBundle();
    NSBundle *nsBundle = [NSBundle mainBundle];
    const auto background =
        cfBundle ? CFBundleGetValueForInfoDictionaryKey(cfBundle, CFSTR("LSBackgroundOnly")) : nullptr;
    id nsBackground = [nsBundle objectForInfoDictionaryKey:@"LSBackgroundOnly"];
    if (!cfBundle || !background || CFGetTypeID(background) != CFBooleanGetTypeID() ||
        !CFBooleanGetValue(static_cast<CFBooleanRef>(background)) ||
        bundleString(cfBundle, kCFBundleIdentifierKey) != expectedId ||
        bundleString(cfBundle, kCFBundleExecutableKey) != kCoreBinName ||
        ![nsBackground isKindOfClass:[NSNumber class]] || ![nsBackground boolValue] ||
        bundleString(nsBundle, @"CFBundleIdentifier") != expectedId ||
        bundleString(nsBundle, @"CFBundleExecutable") != kCoreBinName) {
      qCritical("The macOS core is missing its embedded background-only application metadata");
      return false;
    }

    return true;
  }
}

bool MacCoreApplicationBootstrap::finish() const
{
  @autoreleasepool {
    if (![NSThread isMainThread] || !NSApp) {
      qCritical("The macOS core's background policy must be checked on the main thread after QApplication");
      return false;
    }

    // Never hide an initialization error by demoting the process afterward:
    // a transient Regular registration can already have entered Dock history.
    if ([NSApp activationPolicy] != NSApplicationActivationPolicyProhibited) {
      qCritical("The macOS core was not background-only during Qt initialization");
      return false;
    }
  }
  return true;
}

} // namespace deskflow
