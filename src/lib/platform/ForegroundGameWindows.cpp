/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/ForegroundGame.h"

#include <QFileInfo>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <array>

namespace deskflow {
namespace {
class PhysicalCoordinates
{
public:
  PhysicalCoordinates() : m_previous(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
  {
  }
  ~PhysicalCoordinates()
  {
    if (m_previous)
      SetThreadDpiAwarenessContext(m_previous);
  }
  bool valid() const
  {
    return m_previous != nullptr;
  }

private:
  DPI_AWARENESS_CONTEXT m_previous;
};
} // namespace

ForegroundGameState queryForegroundGame(const QStringList &gameApps, const QString &)
{
  ForegroundGameState state;
  const PhysicalCoordinates coordinates;
  if (!coordinates.valid()) {
    state.detail = QStringLiteral("Unable to inspect windows in physical display coordinates");
    return state;
  }

  state.supported = true;
  const HWND window = GetForegroundWindow();
  if (!window || window == GetDesktopWindow() || window == GetShellWindow() || !IsWindowVisible(window) ||
      IsIconic(window))
    return state;

  // A maximized decorated window is not fullscreen, including with an auto-hidden taskbar.
  const auto style = GetWindowLongPtr(window, GWL_STYLE);
  if ((style & WS_CAPTION) != 0 || (style & WS_CHILD) != 0)
    return state;

  MONITORINFO monitor{sizeof(MONITORINFO)};
  RECT client{};
  if (!GetMonitorInfo(MonitorFromWindow(window, MONITOR_DEFAULTTONULL), &monitor) || !GetClientRect(window, &client)) {
    state.supported = false;
    state.detail = QStringLiteral("Unable to read foreground window geometry");
    return state;
  }
  POINT topLeft{client.left, client.top};
  POINT bottomRight{client.right, client.bottom};
  if (!ClientToScreen(window, &topLeft) || !ClientToScreen(window, &bottomRight))
    return state;
  const auto &display = monitor.rcMonitor;
  state.fullscreen = coversGameDisplay(
      QRectF(topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y),
      QRectF(display.left, display.top, display.right - display.left, display.bottom - display.top)
  );
  if (!state.fullscreen)
    return state;

  DWORD processId = 0;
  GetWindowThreadProcessId(window, &processId);
  const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
  if (!process) {
    state.supported = false;
    state.detail = QStringLiteral("Unable to identify the foreground application");
    return state;
  }
  std::array<wchar_t, 32768> path{};
  DWORD length = static_cast<DWORD>(path.size());
  const bool gotPath = QueryFullProcessImageNameW(process, 0, path.data(), &length) != FALSE;
  CloseHandle(process);
  if (!gotPath) {
    state.supported = false;
    state.detail = QStringLiteral("Unable to read the foreground executable path");
    return state;
  }
  state.application = QString::fromWCharArray(path.data(), static_cast<qsizetype>(length));
  const QStringList identifiers{state.application, QFileInfo(state.application).fileName()};
  state.game = matchesGameApplication(identifiers, gameApps) || isKnownGamePath(state.application);
  if (!state.game)
    state.detail = QStringLiteral("Fullscreen application is not recognised as a game; an application rule is required");

  // Discard a sample if focus changed while the executable was being inspected.
  if (GetForegroundWindow() != window) {
    state.fullscreen = false;
    state.game = false;
  }
  return state;
}

} // namespace deskflow
