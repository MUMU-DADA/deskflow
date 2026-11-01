/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once
#include "base/Log.h"
#include <QtTest>

class AlignmentTransportTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void codecValidation();
  void realProxyNegotiation();
  void boundedNetworkRead();
  void registeredUiGenerationAndFraming();
  void boundedDragLeases();

private:
  Log m_log;
};
