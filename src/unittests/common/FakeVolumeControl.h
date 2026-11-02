/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "common/VolumeSync.h"
#include <algorithm>

class FakeVolumeControl : public deskflow::volume::Control
{
public:
  deskflow::volume::Snapshot current{"device-one", "Test speakers", {4000, false}, 3};
  bool present = true;
  bool failWrites = false;
  int reads = 0;
  int writes = 0;
  int roundStep = 1;

  std::optional<deskflow::volume::Snapshot> read() override
  {
    ++reads;
    return present ? std::optional(current) : std::nullopt;
  }
  bool write(const QString &device, deskflow::volume::State state, uint8_t mask) override
  {
    ++writes;
    if (!present || failWrites || device != current.deviceId || (current.capabilities & mask) != mask)
      return false;
    if (mask & deskflow::volume::Volume)
      current.state.volume =
          static_cast<uint16_t>(std::min(10000, ((state.volume + roundStep / 2) / roundStep) * roundStep));
    if (mask & deskflow::volume::Mute)
      current.state.muted = state.muted;
    return true;
  }
};
