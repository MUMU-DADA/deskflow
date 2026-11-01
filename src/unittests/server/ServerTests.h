/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/Log.h"
#include <QTest>

class ServerTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void SwitchToComputerInfo_alloc_computer();
  void KeyboardBroadcastInfo_alloc_stateAndComputers();
  void fullscreenGameGuard_activeDeviceOnlyAndExplicitSwitch();
  void fullscreenGameGuard_cancelsDelayedSwitch();
  void fullscreenGameGuard_preservesManualLock();
  void calibrationDragBlocksOnlyActiveEdgesAndEscapeReleases();
  void calibrationMissingMouseUpExpiresDespitePings();
  void calibrationRevisionAliasesAndManualLock();
  void calibrationNativeReleaseRejectsStaleDragHeartbeats();
  void calibrationExplicitSwitchRejectsOldSourceHeartbeats();
  void edgeMappingStaysInsideDestination_data();
  void edgeMappingStaysInsideDestination();

private:
  Log m_log;
};
