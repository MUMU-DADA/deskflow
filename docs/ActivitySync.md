<!-- SPDX-FileCopyrightText: (C) 2026 Deskflow Developers -->
<!-- SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception -->

# Activity synchronization

In **Preferences → General**, enable **Synchronize activity with connected computers** on both computers. The option is off by default and currently supports Windows and macOS. Both ends need a build with this feature. Older peers continue to share input normally without activity synchronization.

Keyboard presses, mouse movement, buttons and scrolling on either computer keep the connected screens awake. This includes a client's own keyboard/mouse even while the shared cursor is on the server. Only a new input observation triggers a notification, at most once per second per sender. When all input stops, no further notifications are sent and each operating system uses its own configured display-sleep timeout, measured from the last activity (plus sampling/network latency). System sleep settings are never edited.

Leave **Prevent this computer from going to sleep** off if you want normal idle sleep. That existing option independently keeps the local computer awake for as long as its core runs. Activity synchronization does not unlock computers or override explicit system sleep.

The setting is `core/syncUserActivity` in `Deskflow.conf`. Apply preferences and allow the usual core restart. Disabling the option or disconnecting stops synchronization and releases its macOS power assertion.

The optional `ASCP=1` server option negotiates the `DACT%1i` message. Clients explicitly opt in (`1`) or out (`0`); `2` reports a new input observation. The server relays activity only to participating clients, never back to its source. Unnegotiated or invalid messages are rejected. Connection heartbeats, clipboard changes and game detection are not input activity.

Windows observes the session's last-input timestamp and resets the display/system idle timers with a one-shot power-management call. macOS observes keyboard/mouse event counters and declares user activity with an assertion that expires using the system's display-sleep setting. Neither receiver synthesizes key presses or mouse movement, and received power notifications are never counted as input. Reconnection and long polling gaps establish a new baseline rather than replaying old input.

Implementation references: [Windows one-shot idle reset](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setthreadexecutionstate), [macOS event source counters](https://developer.apple.com/documentation/coregraphics/cgeventsource?language=objc), [macOS user activity declaration](https://github.com/apple-oss-distributions/IOKitUser/blob/main/pwr_mgt.subproj/IOPMLib.h).
