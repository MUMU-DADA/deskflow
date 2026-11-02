/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "common/VolumeSync.h"

namespace deskflow::volume {
bool systemVolumeSupported();
std::unique_ptr<Control> makeSystemVolumeControl();
} // namespace deskflow::volume
