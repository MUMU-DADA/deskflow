/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/ForegroundGame.h"

#include <QFileInfo>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include <vector>

namespace deskflow {
namespace {
QString fromNSString(NSString *value)
{
  return value ? QString::fromUtf8([value UTF8String]) : QString();
}

bool readBoolean(AXUIElementRef element, CFStringRef attribute)
{
  CFTypeRef value = nullptr;
  if (AXUIElementCopyAttributeValue(element, attribute, &value) != kAXErrorSuccess || !value) {
    if (value)
      CFRelease(value);
    return false;
  }
  const bool result = CFGetTypeID(value) == CFBooleanGetTypeID() && CFBooleanGetValue((CFBooleanRef)value);
  CFRelease(value);
  return result;
}

bool focusedWindowBounds(pid_t pid, CGRect &bounds, bool &nativeFullscreen, bool &decorated, bool &decorationKnown)
{
  AXUIElementRef application = AXUIElementCreateApplication(pid);
  if (!application)
    return false;
  // A hung game must not stall the core input event loop for the default AX timeout.
  AXUIElementSetMessagingTimeout(application, 0.02f);
  CFTypeRef focused = nullptr;
  const auto result = AXUIElementCopyAttributeValue(application, kAXFocusedWindowAttribute, &focused);
  CFRelease(application);
  if (result != kAXErrorSuccess || !focused) {
    if (focused)
      CFRelease(focused);
    return false;
  }
  if (CFGetTypeID(focused) != AXUIElementGetTypeID()) {
    CFRelease(focused);
    return false;
  }
  auto window = (AXUIElementRef)focused;
  AXUIElementSetMessagingTimeout(window, 0.02f);
  CFTypeRef position = nullptr;
  CFTypeRef size = nullptr;
  AXUIElementCopyAttributeValue(window, kAXPositionAttribute, &position);
  AXUIElementCopyAttributeValue(window, kAXSizeAttribute, &size);
  bool valid =
      position && size && CFGetTypeID(position) == AXValueGetTypeID() && CFGetTypeID(size) == AXValueGetTypeID();
  if (valid) {
    valid = AXValueGetValue((AXValueRef)position, static_cast<AXValueType>(kAXValueCGPointType), &bounds.origin) &&
            AXValueGetValue((AXValueRef)size, static_cast<AXValueType>(kAXValueCGSizeType), &bounds.size);
  }
  if (position)
    CFRelease(position);
  if (size)
    CFRelease(size);

  nativeFullscreen = readBoolean(window, CFSTR("AXFullScreen"));
  CFTypeRef closeButton = nullptr;
  const auto buttonResult = AXUIElementCopyAttributeValue(window, kAXCloseButtonAttribute, &closeButton);
  decorationKnown = buttonResult == kAXErrorSuccess || buttonResult == kAXErrorNoValue;
  decorated = buttonResult == kAXErrorSuccess && closeButton && CFGetTypeID(closeButton) == AXUIElementGetTypeID();
  if (closeButton)
    CFRelease(closeButton);
  const bool minimized = readBoolean(window, kAXMinimizedAttribute);
  CFRelease(window);
  // Native fullscreen is authoritative even when the game does not publish
  // usable geometry, or macOS reserves part of a notched display for compatibility.
  return !minimized && (valid || nativeFullscreen);
}

bool coversDisplay(const CGRect &bounds)
{
  uint32_t count = 0;
  if (CGGetActiveDisplayList(0, nullptr, &count) != kCGErrorSuccess || count == 0)
    return false;
  std::vector<CGDirectDisplayID> displays(count);
  if (CGGetActiveDisplayList(count, displays.data(), &count) != kCGErrorSuccess)
    return false;
  const QRectF window(bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height);
  for (uint32_t i = 0; i < count; ++i) {
    const auto display = CGDisplayBounds(displays[i]);
    if (coversGameDisplay(window, QRectF(display.origin.x, display.origin.y, display.size.width, display.size.height)))
      return true;
  }
  return false;
}
} // namespace

ForegroundGameState queryForegroundGame(const QStringList &gameApps, const QString &, const QString &)
{
  @autoreleasepool {
    ForegroundGameState state;
    // NSWorkspace foreground state is delivered through the main Cocoa run loop.
    // The caller must keep that loop running while this worker samples; the
    // standalone probe pumps it between samples and while waiting for each query.
    NSRunningApplication *application = [[NSWorkspace sharedWorkspace] frontmostApplication];
    if (!application) {
      state.detail = QStringLiteral("No foreground application is available in this session");
      return state;
    }
    state.supported = true;
    if ([application isHidden] || [application isTerminated])
      return state;

    const auto executable = fromNSString([[application executableURL] path]);
    const auto bundlePath = fromNSString([[application bundleURL] path]);
    const auto bundleId = fromNSString([application bundleIdentifier]);
    state.application = bundleId.isEmpty() ? executable : bundleId;
    const QStringList identifiers{bundleId, bundlePath, executable, QFileInfo(executable).fileName()};
    NSBundle *bundle = [application bundleURL] ? [NSBundle bundleWithURL:[application bundleURL]] : nil;
    id categoryValue = [bundle objectForInfoDictionaryKey:@"LSApplicationCategoryType"];
    const auto category = [categoryValue isKindOfClass:[NSString class]] ? fromNSString(categoryValue) : QString();
    const bool gameCategory =
        category == QStringLiteral("public.app-category.games") ||
        (category.startsWith(QStringLiteral("public.app-category.")) && category.endsWith(QStringLiteral("-games")));
    state.game = matchesGameApplication(identifiers, gameApps) || gameCategory ||
                 (category.isEmpty() && isKnownGamePath(executable));
    const auto pid = [application processIdentifier];
    CGRect bounds{};
    bool nativeFullscreen = false;
    bool decorated = false;
    bool decorationKnown = false;
    // Check passively: the standalone probe must never request or change TCC grants.
    if (!AXIsProcessTrusted()) {
      state.supported = false;
      state.detail = QStringLiteral("Accessibility permission is missing for this application");
      return state;
    }
    if (!focusedWindowBounds(pid, bounds, nativeFullscreen, decorated, decorationKnown)) {
      state.supported = false;
      state.detail = QStringLiteral("Foreground window is not accessible; fullscreen game protection is unavailable");
      return state;
    }
    // Screen-sized CG bounds alone do not establish that a window is borderless:
    // a maximized window can also fill the display with hidden menu bar and Dock.
    const bool displayCovered = coversDisplay(bounds);
    if (displayCovered && !nativeFullscreen && !decorationKnown) {
      state.supported = false;
      state.detail = QStringLiteral("Foreground window does not expose fullscreen or decoration state");
      return state;
    }
    state.fullscreen = nativeFullscreen || (displayCovered && !decorated);
    if (state.fullscreen && !state.game)
      state.detail =
          QStringLiteral("Fullscreen application is not recognised as a game; an application rule is required");
    if ([[[NSWorkspace sharedWorkspace] frontmostApplication] processIdentifier] != pid)
      state.fullscreen = false;
    return state;
  }
}

} // namespace deskflow
