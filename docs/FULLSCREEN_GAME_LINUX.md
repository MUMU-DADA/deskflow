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
| KDE Plasma 6 Wayland | The optional KWin script and session helper below |
| Other compositors | An adapter producing the JSON contract below is required |

Game identity comes from an installed desktop entry with `Categories=Game`,
Steam game metadata/paths, or `core/fullscreenGameApps` application rules. Native
Wayland focus must be queried from the compositor. XWayland's last focused X11
window is deliberately not used as a fallback: it may still name a game after
the user has switched to a native Wayland application.

X11 builds additionally require the `xcb` and `xcb-randr` development packages.
On Debian/Ubuntu these are `libxcb1-dev` and `libxcb-randr0-dev`.
Detection uses the same `core/display` selected when the native X11 backend
starts. Changing this setting requires restarting the core; the standalone
probe instead uses its process's `DISPLAY` environment.

## Hyprland coverage limit

The built-in Hyprland detector currently requires the compositor's effective
fullscreen flag. A borderless game that covers its monitor while Hyprland still
reports `fullscreen: 0` is **not detected** by this backend. Such a case requires
the state-file adapter below. Merely comparing `activewindow` geometry against
`monitors` is insufficient: it does not establish the absence of decorations
drawn inside the application's own surface. The `decorations` query describes
compositor decorations, not all client-side decorations, and `fullscreenClient`
can be set while the compositor keeps a window tiled or maximized.

This limitation is deliberately not hidden by treating every monitor-sized
game window as fullscreen. See Hyprland's [query documentation](https://wiki.hypr.land/configuring/core/advanced-configuration/using-hyprctl/)
and [IPC serialization](https://github.com/hyprwm/Hyprland/blob/4b9ac40d71ceeeb027cfa06992c9212e3c3077b6/src/ipc/s1/Commands.cpp).

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

## KDE Plasma 6 adapter

The optional `deploy/linux/foreground-game-kwin` package observes KWin's actual
active window and fullscreen state, including borderless monitor-sized windows.
A small session-bus helper writes the same JSON contract. The helper accepts
updates only from the process currently owning `org.kde.KWin`; it does not run
commands received over D-Bus. Application categories/rules are evaluated by
Deskflow, not by the helper. The adapter requires runtime validation on Plasma;
its source and helper unit tests do not establish end-to-end desktop support.

Install the distribution's Python D-Bus and GLib bindings. On Debian/Ubuntu the
packages are `python3-dbus` and `python3-gi`. Then, from the repository root:

```sh
kpackagetool6 --type=KWin/Script --install deploy/linux/foreground-game-kwin
mkdir -p "$HOME/.local/libexec" "$HOME/.config/systemd/user"
cp deploy/linux/foreground-game-kwin/deskflow-foreground-writer.py "$HOME/.local/libexec/"
cp deploy/linux/foreground-game-kwin/deskflow-foreground-writer.service "$HOME/.config/systemd/user/"
systemctl --user daemon-reload
systemctl --user enable --now deskflow-foreground-writer.service
```

Enable **Deskflow Foreground Game Guard** in System Settings → Window Management
→ KWin Scripts. Set Deskflow's `core/fullscreenGameStateFile` to the absolute
expanded path of `$XDG_RUNTIME_DIR/deskflow-foreground-game.json`. Use application
rules for games whose desktop entries are missing or do not declare `Game`.

For desktops without systemd, start `python3 deskflow-foreground-writer.py` in
the same user session instead and add it to the desktop's session autostart.
The runtime directory must be private and owned by that user. Disabling the
KWin script stops refreshes; the state then expires after 1500 ms. Stopping the
helper removes its file. Normal input processing never waits for these adapters.

The helper's tests need only Python's standard library:

```sh
python3 deploy/linux/foreground-game-kwin/test_writer.py
```

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
GNOME requires the included extension; KDE Plasma 6 requires the included KWin script and session helper.

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
