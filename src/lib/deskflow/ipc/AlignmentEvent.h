/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "base/Event.h"
#include <QJsonObject>
#include <utility>

struct AlignmentEventData : EventData
{
  enum class Kind
  {
    Initialize,
    UiAvailability,
    Message
  };
  explicit AlignmentEventData(Kind kind, QJsonObject message = {}, uint64_t generation = 0, bool available = false)
      : kind(kind),
        message(std::move(message)),
        generation(generation),
        available(available)
  {
  }
  Kind kind;
  QJsonObject message;
  uint64_t generation;
  bool available;
};
