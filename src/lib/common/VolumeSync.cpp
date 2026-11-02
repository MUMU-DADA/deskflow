/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "VolumeSync.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

namespace deskflow::volume {
std::optional<Message> decode(uint8_t kind, uint32_t epoch, uint32_t sequence, uint16_t level, uint8_t flags)
{
  if (kind > 3 || epoch == 0 || level > Maximum || (flags & ~7) != 0)
    return std::nullopt;
  Message result{static_cast<Kind>(kind), epoch, sequence, {level, (flags & 4) != 0}, uint8_t(flags & 3)};
  if (result.kind == Kind::Unavailable) {
    if (sequence != 0 || level != 0 || flags != 0)
      return std::nullopt;
  } else if (result.kind == Kind::Ready) {
    if (sequence != 0 || !(result.mask & Volume))
      return std::nullopt;
  } else if (sequence == 0 || result.mask == 0) {
    return std::nullopt;
  }
  return result;
}

bool newer(uint32_t sequence, uint32_t previous)
{
  const uint32_t delta = sequence - previous;
  return sequence != 0 && (previous == 0 || (delta != 0 && delta < 0x80000000u));
}

uint32_t next(uint32_t value)
{
  return value == UINT32_MAX ? 1 : value + 1;
}

State scale(State source, int sourceReference, int targetReference)
{
  if (sourceReference < 1 || sourceReference > 100 || targetReference < 1 || targetReference > 100)
    return source;
  const auto value = (uint32_t(source.volume) * targetReference + sourceReference / 2) / sourceReference;
  return {static_cast<uint16_t>(std::min<uint32_t>(Maximum, value)), source.muted};
}

std::optional<Weights> parseWeights(const QString &json)
{
  if (json.size() > 65536)
    return std::nullopt;
  const auto document = QJsonDocument::fromJson(json.toUtf8());
  if (!document.isObject() || document.object().size() > 128)
    return std::nullopt;
  Weights weights;
  const auto object = document.object();
  for (auto i = object.begin(); i != object.end(); ++i) {
    const auto value = i.value().toDouble(-1);
    if (i.key().trimmed().isEmpty() || i.key().size() > 255 || i.key().contains(QChar(0)) || !i.value().isDouble() ||
        !std::isfinite(value) || value < 1 || value > 100 || std::floor(value) != value)
      return std::nullopt;
    weights.insert(i.key(), static_cast<int>(value));
  }
  return weights;
}

QString serializeWeights(const Weights &weights)
{
  QJsonObject object;
  for (auto i = weights.begin(); i != weights.end(); ++i)
    object.insert(i.key(), i.value());
  return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

int reference(const Weights &weights, const QString &name)
{
  return weights.value(name, 100);
}

Monitor::Monitor(std::unique_ptr<Control> control) : m_control(std::move(control))
{
}

bool Monitor::poll(Clock::time_point now)
{
  if (m_lastPoll && now - *m_lastPoll < std::chrono::milliseconds(100))
    return false;
  m_lastPoll = now;
  auto current = m_control->read();
  m_changes = 0;
  m_deviceChanged = current.has_value() != m_snapshot.has_value() ||
                    (current && m_snapshot &&
                     (current->deviceId != m_snapshot->deviceId || current->capabilities != m_snapshot->capabilities));
  if (current && m_snapshot && !m_deviceChanged) {
    if (current->state.volume != m_snapshot->state.volume)
      m_changes |= Volume;
    if ((current->capabilities & Mute) && current->state.muted != m_snapshot->state.muted)
      m_changes |= Mute;
  }
  m_snapshot = std::move(current);
  return true;
}

bool Monitor::apply(State state, uint8_t mask)
{
  const auto current = m_control->read();
  if (!m_snapshot || !current || current->deviceId != m_snapshot->deviceId || (current->capabilities & mask) != mask ||
      state.volume > Maximum) {
    m_lastPoll.reset();
    return false;
  }
  const bool success = m_control->write(current->deviceId, state, mask);
  const auto actual = m_control->read();
  if (!actual || actual->deviceId != current->deviceId) {
    m_lastPoll.reset();
    return false;
  }
  // Preserve unpolled LOCAL changes in fields that this update did not write.
  if (mask & Volume)
    m_snapshot->state.volume = actual->state.volume;
  if (mask & Mute)
    m_snapshot->state.muted = actual->state.muted;
  return success;
}

void Monitor::reset()
{
  m_snapshot.reset();
  m_lastPoll.reset();
  m_changes = 0;
  m_deviceChanged = false;
}

ClientSession::ClientSession(std::unique_ptr<Control> control) : m_monitor(std::move(control))
{
}

void ClientSession::enable(bool enabled)
{
  if (m_enabled == enabled)
    return;
  m_enabled = enabled;
  m_monitor.reset();
  m_initialized = m_announced = false;
  m_retryAt.reset();
}

std::optional<Message> ClientSession::poll(Clock::time_point now)
{
  if (!m_enabled || (m_retryAt && now < *m_retryAt))
    return std::nullopt;
  if (m_retryAt) {
    m_retryAt.reset();
    m_announced = false;
    m_monitor.reset();
  }
  if (!m_monitor.poll(now))
    return std::nullopt;
  const auto &snapshot = m_monitor.snapshot();
  if (!m_announced || m_monitor.deviceChanged()) {
    m_announced = true;
    m_initialized = false;
    m_sequence = m_lastSet = 0;
    m_epoch = next(m_epoch);
    if (!snapshot || !(snapshot->capabilities & Volume))
      return Message{Kind::Unavailable, m_epoch, 0, {}, 0};
    return Message{Kind::Ready, m_epoch, 0, snapshot->state, snapshot->capabilities};
  }
  if (!snapshot || !m_initialized || m_monitor.changes() == 0)
    return std::nullopt;
  m_sequence = next(m_sequence);
  return Message{Kind::Change, m_epoch, m_sequence, snapshot->state, m_monitor.changes()};
}

ReceiveResult ClientSession::receive(const Message &message, Clock::time_point now)
{
  if (!m_enabled || message.kind != Kind::Set ||
      !decode(uint8_t(message.kind), message.epoch, message.sequence, message.state.volume, message.flags()))
    return ReceiveResult::Invalid;
  if (message.epoch != m_epoch || !newer(message.sequence, m_lastSet))
    return ReceiveResult::Ignored;
  if (!m_initialized && !(message.mask & Volume))
    return ReceiveResult::Ignored;
  m_lastSet = message.sequence;
  if (!m_monitor.apply(message.state, message.mask)) {
    m_initialized = false;
    if (!m_retryAt)
      m_retryAt = now + std::chrono::seconds(1);
    return ReceiveResult::Failed;
  }
  m_initialized = true;
  m_retryAt.reset();
  return ReceiveResult::Applied;
}

void PeerSession::reset(bool advertised)
{
  m_advertised = advertised;
  m_capabilities = 0;
  m_initialized = false;
  m_epoch = m_lastChange = m_sequence = 0;
  m_pending.reset();
}

bool PeerSession::receive(const Message &message)
{
  if (!m_advertised || message.kind == Kind::Set ||
      !decode(uint8_t(message.kind), message.epoch, message.sequence, message.state.volume, message.flags()))
    return false;
  if (message.kind == Kind::Ready || message.kind == Kind::Unavailable) {
    if (message.epoch == m_epoch)
      return true;
    m_epoch = message.epoch;
    m_capabilities = message.kind == Kind::Ready ? message.mask : 0;
    m_initialized = false;
    m_lastChange = m_sequence = 0;
    m_pending.reset();
    return true;
  }
  if (message.epoch != m_epoch || !m_initialized || !newer(message.sequence, m_lastChange))
    return true;
  if ((message.mask & m_capabilities) != message.mask)
    return false;
  m_lastChange = message.sequence;
  if (m_pending) {
    // Keep both components if several independent volume/mute changes coalesce.
    if (!(message.mask & Volume))
      m_pending->state.muted = message.state.muted;
    else if (!(message.mask & Mute))
      m_pending->state.volume = message.state.volume;
    else
      m_pending->state = message.state;
    m_pending->mask |= message.mask;
    m_pending->sequence = message.sequence;
  } else {
    m_pending = message;
  }
  return true;
}

std::optional<Message> PeerSession::takeChange()
{
  auto result = m_pending;
  m_pending.reset();
  return result;
}

std::optional<Message> PeerSession::set(State state, uint8_t mask)
{
  mask &= capabilities();
  if (!(capabilities() & Volume) || mask == 0 || (!m_initialized && !(mask & Volume)))
    return std::nullopt;
  m_sequence = next(m_sequence);
  m_initialized = true;
  return Message{Kind::Set, m_epoch, m_sequence, state, mask};
}
} // namespace deskflow::volume
