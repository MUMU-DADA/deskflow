/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

namespace deskflow {

// Check the embedded background declaration before Qt creates QNSApplication,
// then check the native policy immediately afterward, both on the main thread.
class MacCoreApplicationBootstrap
{
public:
  bool prepare() const;
  bool finish() const;
};

} // namespace deskflow
