<!-- SPDX-FileCopyrightText: (C) 2026 Deskflow Developers -->
<!-- SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception -->

# Activity synchronization

On the **server**, open **Preferences → General** and enable **Keep client screens awake during server activity**. The option is off by default and currently supports Windows and macOS. Compatible clients automatically accept activity notifications; no client-side switch is needed. Peers without this feature continue to share input normally without activity synchronization.

Keyboard presses, mouse movement, buttons and scrolling on the server keep all connected client screens awake. This also works when the server's keyboard/mouse is controlling a client, and preserves the server's own activity for input captured by its hooks. A client's own keyboard/mouse affects only that client's normal local activity: it never refreshes the server or other clients. Only new server input triggers a notification, at most once per second. When server input stops, no further notifications are sent and clients use their own configured display-sleep timeouts, measured from their last local or shared activity (plus sampling/network latency). System sleep settings are never edited.

Leave **Prevent this computer from going to sleep** off if you want normal idle sleep. That existing option independently keeps the local computer awake for as long as its core runs. Activity synchronization does not unlock computers or override explicit system sleep.

The server setting is `core/syncUserActivity` in `Deskflow.conf`. Apply preferences and allow the usual core restart. The stored value is ignored in client mode and the checkbox is shown only in server mode. Disabling the server option or disconnecting stops synchronization and releases the client's macOS power assertion.

The optional `ASCP=1` server option negotiates the `DACT%1i` message. Clients reply with supported (`1`) or unsupported (`0`); only the server sends new-input notifications (`2`). The server sends them to every capable client. Earlier bidirectional test clients may still send `2`: an opted-in legacy client's notifications are consumed and ignored, without refreshing any display or breaking the connection. Those older clients still require their old local option to be enabled until upgraded. Unnegotiated or invalid messages are rejected. Connection heartbeats, clipboard changes and game detection are not input activity.

A Windows server observes the session's last-input timestamp; a macOS server observes keyboard/mouse event counters. Both also observe captured server input while controlling a client. Receiving Windows clients reset display/system idle timers with a one-shot power-management call; macOS clients declare user activity with an assertion that expires using their display-sleep setting. Clients never sample input for this feature, synthesize key presses/mouse movement, or send activity back. Reconnection and long polling gaps establish a new server baseline rather than replaying old input.

Implementation references: [Windows one-shot idle reset](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setthreadexecutionstate), [macOS event source counters](https://developer.apple.com/documentation/coregraphics/cgeventsource?language=objc), [macOS user activity declaration](https://github.com/apple-oss-distributions/IOKitUser/blob/main/pwr_mgt.subproj/IOPMLib.h).
