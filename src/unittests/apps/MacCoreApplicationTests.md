<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
SPDX-License-Identifier: MIT
-->

# macOS core startup regression

Run `ctest --test-dir build/src/unittests -R MacCoreApplicationTests --output-on-failure`
on a macOS build machine with a logged-in graphical session and no running Deskflow core.
The test forces the native Cocoa plugin in its child processes even when other tests use
`QT_QPA_PLATFORM=offscreen`. It does not install input hooks or connect to another device.

The children run as secondary executables inside an ordinary test app bundle without
background-only metadata, matching the core's placement inside the GUI bundle. They
inspect AppKit's actual activation policy immediately after constructing QApplication,
before the explicit background policy is applied. A negative control checks that Qt's
normal initialization would promote the process to Regular, then promptly demotes it.
The normal case must stay Prohibited. Existing empty/nonempty/unset environment values
must be restored before user-configured child commands could inherit the environment.
The background process must also deliver a queued Qt event and provide a clipboard object.

The production core entry point is also compiled with only its `main` symbol renamed.
Isolated children invoke its actual help, version and duplicate-instance return paths,
then inspect the native policy after return. The duplicate test holds the normal core
shared-memory key, so this suite belongs on an isolated build machine, not a live client.

The suppression variable is a private Qt Cocoa implementation switch, verified against
Qt 6.9.3 (`qcocoaintegration.mm` and `qcocoahelpers.mm`). These native checks deliberately
fail if a Qt upgrade stops honoring it. They do not prove Dock recent-history behavior,
login/reboot behavior, clipboard transfer, permissions, or live keyboard/mouse sharing;
those require testing the packaged application on the intended macOS release.
