/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace deskflow {

// Observes input without generating input. A received activity notification only
// resets display idleness; it must never become a new outgoing notification.
class ActivitySync
{
public:
  using Clock = std::chrono::steady_clock;
  ActivitySync() = default;
  virtual ~ActivitySync();
  ActivitySync(const ActivitySync &) = delete;
  ActivitySync &operator=(const ActivitySync &) = delete;

  static bool supported();
  bool poll(Clock::time_point now = Clock::now());
  void noteInput();
  void receive(Clock::time_point now = Clock::now());
  void reset();

protected:
  virtual std::optional<uint64_t> inputToken() const;
  virtual void refreshDisplay();
  virtual void releaseDisplay();

private:
  std::optional<uint64_t> m_token;
  std::optional<Clock::time_point> m_lastPoll;
  std::optional<Clock::time_point> m_lastRefresh;
  bool m_pendingInput = false;
  uint32_t m_assertion = 0;
};

} // namespace deskflow
