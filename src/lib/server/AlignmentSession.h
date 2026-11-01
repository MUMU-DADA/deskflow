/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <chrono>
#include <utility>

// Event-thread-only, transient state. Independent of Scroll Lock and settings.
class AlignmentSession
{
public:
  using Clock = std::chrono::steady_clock;
  using Time = Clock::time_point;
  QString id;
  QSet<QString> participants;

  bool join(const QString &session, const QString &name, Time now = Clock::now())
  {
    if (!id.isEmpty() && id != session)
      return false;
    id = session;
    participants.insert(name);
    touch(now);
    return true;
  }
  void touch(Time now = Clock::now())
  {
    expires = now + std::chrono::seconds(10);
  }
  bool expired(Time now = Clock::now()) const
  {
    return !id.isEmpty() && now >= expires;
  }
  bool point(const QString &name, uint64_t seq, bool dragging, Time now = Clock::now())
  {
    if (!participants.contains(name) || (sequences.contains(name) && seq <= sequences.value(name)))
      return false;
    sequences.insert(name, seq);
    if (dragging)
      dragLeases.insert(name, now + std::chrono::seconds(3));
    else
      dragLeases.remove(name);
    touch(now);
    return true;
  }
  void mouseDown(uint32_t button, const QString &name, Time now = Clock::now())
  {
    if (!id.isEmpty() && participants.contains(name))
      buttons.insert(button, {name, now + std::chrono::seconds(3)});
  }
  void mouseUp(uint32_t button)
  {
    if (const auto it = buttons.constFind(button); it != buttons.cend())
      dragLeases.remove(it.value().first);
    buttons.remove(button);
  }
  bool blocks(const QString &name, Time now = Clock::now()) const
  {
    if (id.isEmpty() || expired(now))
      return false;
    for (const auto &button : buttons) {
      if (button.first == name && button.second > now)
        return true;
    }
    return dragLeases.contains(name) && dragLeases.value(name) > now;
  }
  void clear()
  {
    *this = {};
  }

  void showRevision(const QString &name, int revision)
  {
    revisions.insert(name, revision);
    sequences.remove(name);
    dragLeases.remove(name);
  }
  bool matchesRevision(const QString &name, int revision) const
  {
    return revisions.contains(name) && revisions.value(name) == revision;
  }

private:
  Time expires{};
  QHash<QString, uint64_t> sequences;
  QHash<QString, int> revisions;
  QHash<QString, Time> dragLeases;
  QHash<uint32_t, std::pair<QString, Time>> buttons;
};
