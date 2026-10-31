<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
-->

# Fullscreen game protection

This fork can inhibit automatic mouse transitions while the computer currently
receiving shared input has a foreground game in fullscreen or borderless
fullscreen mode. It applies to both server and client computers. Background
games on other computers do not inhibit switching. Leaving the game or its
fullscreen mode releases automatic protection. Explicit computer-switching
hotkeys still work; manual cursor locking remains independent.

Install the modified version on both ends for client-side protection. The
protocol extension is negotiated through an optional server capability, so an
older peer remains usable but cannot provide client game status.

## Configuration

Preferences → Games contains the enable switch (on by default) and additional
game applications, one per line. Use an exact executable name, executable path,
or platform application identifier. For example `game.exe` on Windows or a
bundle identifier on macOS. A rule identifies a game; it does not bypass the
foreground/fullscreen requirement. Restart the core after applying preferences
if the existing GUI requests it.

The corresponding settings are in the `[core]` section of `Deskflow.conf`:

| Setting | Default | Meaning |
| --- | --- | --- |
| `enableFullscreenGameGuard` | `true` | Enable automatic protection on this computer |
| `fullscreenGameApps` | empty list | Additional exact game application identifiers |
| `fullscreenGameStateFile` | empty | Optional Wayland compositor integration file |

Game identity cannot be inferred reliably from window dimensions alone. Native
application categories, desktop entries, Steam game identifiers/library paths,
and the additional application rules provide identification. Steam library
paths are a heuristic and can include non-game utilities; known common utilities
are excluded. Use the feature switch if an application is misidentified.

## Platform detection

* Windows: foreground executable and undecorated client area covering its monitor,
  measured in physical coordinates for mixed-DPI displays.
* macOS: frontmost application identity/category and Accessibility window state
  and geometry. Deskflow needs its normal Accessibility permission. A game that
  does not expose sufficient window information cannot be classified reliably.
* Linux/BSD X11: EWMH active window/fullscreen state and XCB monitor geometry.
* Linux Wayland: compositor-specific adapters are required. Sway and Hyprland
  expose suitable IPC; GNOME and KDE Plasma 6 have optional desktop adapters
  supplied in this fork. Other desktops require an integration that supplies the
  documented state file. See [Linux integration](FULLSCREEN_GAME_LINUX.md).

An unavailable detector does not mean the game is protected. Diagnostic messages
identify missing window access or unsupported desktop integrations. There is no
portable API guaranteeing detection for every game on every operating system.

## Timing and protocol

Detection runs outside the input event loop with one query in flight. Samples
and client status are updated periodically, so a focus/fullscreen change has a
short detection delay rather than an instantaneous operating-system guarantee.
Entry briefly holds automatic edge switching while a fresh observation arrives.
Explicit switching remains available during that hold.

The server advertises `FGCP=1` through the existing options packet. Supporting
clients send `DFSG` with the current enter sequence and a boolean guard state.
Stale sequences are ignored, and leave/disconnect clears connection state. The
existing Barrier/Synergy version numbers are unchanged. Only the active computer
can affect edge switching, including delayed and double-tap switching paths.

## Validation before release

Automated coverage exercises application matching, display geometry, protocol
negotiation and stale/inactive client reports. Cross-platform builds run in the
`Fullscreen game guard` workflow. These checks do not replace physical testing:

1. Test a game on the server and then on the client, in exclusive fullscreen,
   borderless fullscreen and ordinary windowed modes.
2. Check every configured edge, delayed/double-tap switching and multiple monitors
   with different DPI settings.
3. Alt-Tab / Command-Tab away, exit fullscreen, close the game and disconnect the
   active client. Protection must clear without changing a manual lock.
4. Use explicit switch hotkeys while protected, including returning to a game
   that remains fullscreen on the other computer.
5. Confirm fullscreen browsers/videos and background games do not lock switching.
6. Verify old-peer connections remain functional and clearly lack remote protection.
