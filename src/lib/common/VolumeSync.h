/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QMap>
#include <QString>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace deskflow::volume {
inline constexpr uint8_t Volume = 1;
inline constexpr uint8_t Mute = 2;
inline constexpr uint16_t Maximum = 10000; // hundredths of a percent
using Clock = std::chrono::steady_clock;
using Weights = QMap<QString, int>;

struct State
{
  uint16_t volume = 0;
  bool muted = false;
  bool operator==(const State &) const = default;
};

struct Snapshot
{
  QString deviceId;
  QString deviceName;
  State state;
  uint8_t capabilities = 0;
};

class Control
{
public:
  virtual ~Control() = default;
  virtual std::optional<Snapshot> read() = 0;
  virtual bool write(const QString &expectedDevice, State state, uint8_t mask) = 0;
};

enum class Kind : uint8_t
{
  Unavailable = 0,
  Ready = 1,
  Set = 2,
  Change = 3
};

struct Message
{
  Kind kind = Kind::Unavailable;
  uint32_t epoch = 0; // client generation, renewed on reconnect/default-device changes
  uint32_t sequence = 0;
  State state;
  uint8_t mask = 0;
  uint8_t flags() const
  {
    return mask | (state.muted ? 4 : 0);
  }
};

std::optional<Message> decode(uint8_t kind, uint32_t epoch, uint32_t sequence, uint16_t level, uint8_t flags);
bool newer(uint32_t sequence, uint32_t previous);
uint32_t next(uint32_t value);
State scale(State source, int sourceReference, int targetReference);
std::optional<Weights> parseWeights(const QString &json);
QString serializeWeights(const Weights &weights);
int reference(const Weights &weights, const QString &name);

// Reads and applies on the caller's event thread. Readback becomes the baseline
// for fields we wrote, so rounding/clamping can never produce a feedback event.
class Monitor
{
public:
  explicit Monitor(std::unique_ptr<Control> control);
  bool poll(Clock::time_point now = Clock::now());
  bool apply(State state, uint8_t mask);
  void reset();
  const std::optional<Snapshot> &snapshot() const
  {
    return m_snapshot;
  }
  uint8_t changes() const
  {
    return m_changes;
  }
  bool deviceChanged() const
  {
    return m_deviceChanged;
  }

private:
  std::unique_ptr<Control> m_control;
  std::optional<Snapshot> m_snapshot;
  std::optional<Clock::time_point> m_lastPoll;
  uint8_t m_changes = 0;
  bool m_deviceChanged = false;
};

enum class ReceiveResult
{
  Invalid,
  Ignored,
  Applied,
  Failed
};

class ClientSession
{
public:
  explicit ClientSession(std::unique_ptr<Control> control);
  void enable(bool enabled);
  std::optional<Message> poll(Clock::time_point now = Clock::now());
  ReceiveResult receive(const Message &message, Clock::time_point now = Clock::now());

private:
  Monitor m_monitor;
  bool m_enabled = false;
  bool m_announced = false;
  bool m_initialized = false;
  uint32_t m_epoch = 0;
  uint32_t m_sequence = 0;
  uint32_t m_lastSet = 0;
  std::optional<Clock::time_point> m_retryAt;
};

// One instance per connection. Pending changes are coalesced by the proxy into
// one event-queue entry, without dropping the final slider position.
class PeerSession
{
public:
  void reset(bool advertised = false);
  bool receive(const Message &message);
  std::optional<Message> takeChange();
  std::optional<Message> set(State state, uint8_t mask);
  uint8_t capabilities() const
  {
    return m_advertised ? m_capabilities : 0;
  }
  bool initialized() const
  {
    return m_initialized;
  }
  bool hasChange() const
  {
    return m_pending.has_value();
  }

private:
  bool m_advertised = false;
  bool m_initialized = false;
  uint8_t m_capabilities = 0;
  uint32_t m_epoch = 0;
  uint32_t m_lastChange = 0;
  uint32_t m_sequence = 0;
  std::optional<Message> m_pending;
};
} // namespace deskflow::volume
