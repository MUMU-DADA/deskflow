/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "ActivitySync.h"
#include "base/Log.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#elif defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#endif

namespace deskflow {

ActivitySync::~ActivitySync()
{
  ActivitySync::releaseDisplay();
}

bool ActivitySync::supported()
{
#if defined(_WIN32) || defined(__APPLE__)
  return true;
#else
  return false;
#endif
}

bool ActivitySync::poll(Clock::time_point now)
{
  using namespace std::chrono;
  if (m_lastPoll && now - *m_lastPoll < seconds(1))
    return false;

  // Rebaseline after a suspended/stalled event loop rather than replaying input
  // accumulated while disconnected or asleep.
  if (m_lastPoll && now - *m_lastPoll > seconds(5)) {
    m_token.reset();
    m_pendingInput = false;
  }
  m_lastPoll = now;
  const auto token = inputToken();
  const bool changed = token && m_token && *token != *m_token;
  m_token = token;
  const bool input = m_pendingInput || changed;
  m_pendingInput = false;
  return input;
}

void ActivitySync::noteInput()
{
  // Also observe the server's captured input: an OS may omit events consumed
  // by an input hook from its last-input timestamp.
  m_pendingInput = true;
}

void ActivitySync::receive(Clock::time_point now)
{
  if (m_lastRefresh && now - *m_lastRefresh < std::chrono::seconds(1))
    return;
  m_lastRefresh = now;
  refreshDisplay();
}

void ActivitySync::reset()
{
  m_token.reset();
  m_lastPoll.reset();
  m_lastRefresh.reset();
  m_pendingInput = false;
  releaseDisplay();
}

std::optional<uint64_t> ActivitySync::inputToken() const
{
#if defined(_WIN32)
  LASTINPUTINFO info{sizeof(info)};
  if (GetLastInputInfo(&info))
    return info.dwTime;
#elif defined(__APPLE__)
  // Input counters are independent of power-management user-activity assertions.
  // Do not sample HIDIdleTime: resetting that timer can create a feedback loop.
  constexpr CGEventType types[] = {kCGEventLeftMouseDown,     kCGEventLeftMouseUp,      kCGEventRightMouseDown,
                                   kCGEventRightMouseUp,      kCGEventMouseMoved,       kCGEventLeftMouseDragged,
                                   kCGEventRightMouseDragged, kCGEventKeyDown,          kCGEventKeyUp,
                                   kCGEventFlagsChanged,      kCGEventScrollWheel,      kCGEventOtherMouseDown,
                                   kCGEventOtherMouseUp,      kCGEventOtherMouseDragged};
  uint64_t count = 0;
  for (const auto type : types)
    count += CGEventSourceCounterForEventType(kCGEventSourceStateHIDSystemState, type);
  return count;
#endif
  return std::nullopt;
}

void ActivitySync::refreshDisplay()
{
#if defined(_WIN32)
  // One-shot only: do not install a continuous power request or synthesize input.
  if (!SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED))
    LOG_WARN("activity sync: unable to refresh display idle timer");
#elif defined(__APPLE__)
  // The OS expires this assertion using the user's display-sleep interval.
  if (IOPMAssertionDeclareUserActivity(CFSTR("Deskflow shared input activity"), kIOPMUserActiveLocal, &m_assertion) !=
      kIOReturnSuccess)
    LOG_WARN("activity sync: unable to declare display activity");
#endif
}

void ActivitySync::releaseDisplay()
{
#if defined(__APPLE__)
  if (m_assertion) {
    IOPMAssertionRelease(m_assertion);
    m_assertion = 0;
  }
#endif
}

} // namespace deskflow
