<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
SPDX-License-Identifier: MIT
-->

# macOS core startup regression

Run `ctest --test-dir build/src/unittests -R MacCoreApplicationTests --output-on-failure`
on a macOS build machine with a logged-in graphical session and no running Deskflow core.
The test forces the native Cocoa plugin in its child processes even when other tests use
`QT_QPA_PLATFORM=offscreen`. It does not install input hooks or connect to another device.

The children run as `deskflow-core`, a secondary executable inside an ordinary test app
bundle whose outer plist has no background-only declaration. They use the same embedded
`__TEXT,__info_plist` as the production core. Before creating QApplication, they check
that CFBundle and NSBundle both read the core's own identity and Boolean LSBackgroundOnly
flag, without creating NSApplication. Immediately after Qt initialization and after
event processing, the native policy must already be Prohibited. The implementation
does not demote a Regular process afterward to conceal a transient Dock registration.
The private Qt foreground-transform variable is unnecessary and must remain unchanged;
the unset case proves the declaration works without that switch.
The background process must also deliver a queued Qt event and provide a clipboard object.

For a negative control, the same binary is copied to `PolicyHost`, the executable declared
by the outer bundle. That copy must use the ordinary outer metadata, remain Regular, and
be rejected by the core's metadata check before QApplication. This models the GUI's
unchanged metadata and makes the secondary-executable plist precedence observable.

The production core entry point is also compiled with only its `main` symbol renamed.
Isolated children invoke its actual help, version and duplicate-instance return paths,
then inspect the native policy after return. The duplicate test holds the normal core
shared-memory key, so this suite belongs on an isolated build machine, not a live client.

Apple documents [embedded Info.plist sections for single-file tools](https://developer.apple.com/documentation/xcode/build-settings-reference).
Its published [CFBundle implementation](https://github.com/apple-oss-distributions/CF/blob/dc54c6bb1c1e5e0b9486c1d26dd5bef110b20bf3/CFBundle.c#L743)
replaces the parent dictionary for a secondary executable; it does not
merge it. The core plist therefore includes its own Local Network usage description and
automatic/sudden termination settings. Its distinct identity can require renewed system
permission approval; no grants are copied or bypassed. Neither GUI metadata nor pairing
settings are changed. These tests verify current macOS/Qt behavior rather than relying
only on the historical CF implementation. They do not prove Dock recent-history behavior,
login/reboot behavior, clipboard transfer, permissions, or live keyboard/mouse sharing;
those require testing the packaged application on the intended macOS release.
