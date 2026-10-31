<!--
SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
-->

# Foreground game detection on Linux and BSD

The guard uses the same client/server mechanism on every platform. This page
describes the platform-specific source of foreground-window information. Game
identity and fullscreen state are separate checks: a fullscreen video or browser
does not automatically become a game.

| Session | Detection |
| --- | --- |
| X11, including BSD | EWMH active window and fullscreen state; borderless monitor-sized windows via XCB/RandR |
| Sway/i3 | Read-only GET_TREE IPC, including native Wayland and XWayland windows |
| Hyprland | Read-only activewindow IPC and the compositor's internal fullscreen state |
| GNOME Wayland | The optional GNOME Shell adapter below |
| KDE Wayland and other compositors | A compositor adapter producing the JSON contract below is required |

Game identity comes from an installed desktop entry with `Categories=Game`,
Steam game metadata/paths, or `core/fullscreenGameApps` application rules. Native
Wayland focus must be queried from the compositor. XWayland's last focused X11
window is deliberately not used as a fallback: it may still name a game after
the user has switched to a native Wayland application.

X11 builds additionally require the `xcb` and `xcb-randr` development packages.
On Debian/Ubuntu these are `libxcb1-dev` and `libxcb-randr0-dev`.

## GNOME adapter

The optional extension in `deploy/linux/foreground-game-gnome` observes the
focused window inside GNOME Shell. It writes only an application identifier,
fullscreen/game booleans, and a timestamp to the user's runtime directory; it
does not record keystrokes or window titles. This adapter is new and requires
validation in an actual GNOME session; listing a Shell version in metadata is
not a claim that it has been runtime-tested there.

From the repository root:

```sh
extension_dir="$HOME/.local/share/gnome-shell/extensions/foreground-game-guard@deskflow.local"
mkdir -p "$extension_dir"
cp deploy/linux/foreground-game-gnome/extension.js "$extension_dir/"
cp deploy/linux/foreground-game-gnome/metadata.json "$extension_dir/"
```

Log out and back in so GNOME discovers the extension, then enable it:

```sh
gnome-extensions enable foreground-game-guard@deskflow.local
```

Set `core/fullscreenGameStateFile` to the **absolute expanded path** of
`$XDG_RUNTIME_DIR/deskflow-foreground-game.json` in Deskflow's settings. For
example, an actual session may use `/run/user/1000/deskflow-foreground-game.json`;
use your own runtime directory. Environment variables in this setting are not
expanded. Missing desktop metadata can be covered by adding the reported
application identifier to `core/fullscreenGameApps`.

The extension polls every 250 ms. Disabling it removes its state file. Deskflow
reports unavailable detection if the file disappears, is invalid, or becomes
stale. It never treats an unavailable detector as proof that a game is active.

## Compositor adapter contract

Set `core/fullscreenGameStateFile` to a regular JSON file written by an adapter
in the same desktop session. A configured file takes precedence over built-in
Linux detection. A producer must **atomically replace** it at least every 500 ms
and on foreground/fullscreen transitions, using the current state each time:

```json
{
  "version": 1,
  "updatedAtMs": 1793400000000,
  "application": "org.example.Game",
  "fullscreen": true,
  "game": true
}
```

The timestamp above is illustrative; producers must use the current Unix epoch
time in milliseconds. Both timestamp and file modification time must be no more
than 1500 ms old or 250 ms in the future. Files larger than 16 KiB and malformed
types are rejected. `application` is a desktop application ID or process name;
use an empty string and `fullscreen: false` if no application has focus.
`game` is optional: when omitted or false, Deskflow uses its own desktop metadata
and configured application rules. A producer setting it true must establish
game identity itself, independently of fullscreen state.

`fullscreen` means the **focused** window is actually fullscreen or undecorated
and fills its monitor. A window merely requesting fullscreen, being maximized,
or being fullscreen in a background workspace must not produce true. Stop
refreshing the file on query failure; do not keep timestamping stale state.
Deskflow never runs commands from this file or from the setting.

No built-in KDE/GNOME focus query is assumed from the XDG InputCapture portal:
that portal manages input capture, not arbitrary applications' focus metadata.
GNOME requires the included extension; KDE needs a KWin-side adapter.

## Validation

Validate both server and client roles on a real desktop: focus a windowed game,
enter fullscreen, focus a browser, return to the game, exit fullscreen, and
close the game. Also test a fullscreen background game and a fullscreen video.
On Wayland include a native Wayland app and an XWayland app in the focus changes.
Stop the adapter to verify unavailable-state diagnostics and recovery after
restarting it. Parser unit tests cover focus selection and maximized/fullscreen
distinctions, but do not replace compositor integration tests.

References: [EWMH](https://specifications.freedesktop.org/wm/latest-single/),
[Sway IPC](https://man.archlinux.org/man/sway-ipc.7.en),
[Hyprland IPC](https://wiki.hypr.land/0.54.0/IPC/),
[GNOME extension anatomy](https://gjs.guide/extensions/overview/anatomy.html),
[Meta.Window](https://mutter.gnome.org/meta/class.Window.html).
