#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

"""Small user-session bridge from the KWin script to Deskflow's state file.

Requires the distribution's python3-dbus and python3-gi packages. It never
queries applications, executes input, or runs commands supplied over D-Bus.
"""

import json
import os
from pathlib import Path
import signal
import stat
import tempfile
import time


def validate_state(payload, now_ms):
    if not isinstance(payload, str) or len(payload.encode("utf-8")) > 4096:
        raise ValueError("state must be a JSON string no larger than 4096 bytes")
    state = json.loads(payload)
    if not isinstance(state, dict) or type(state.get("version")) is not int or state["version"] != 1:
        raise ValueError("unsupported state schema")
    stamp = state.get("updatedAtMs")
    if type(stamp) is not int or not -250 <= now_ms - stamp <= 1500:
        raise ValueError("state timestamp is stale or invalid")
    app = state.get("application")
    if not isinstance(app, str) or len(app) > 1024 or "\0" in app:
        raise ValueError("invalid application identifier")
    if type(state.get("fullscreen")) is not bool:
        raise ValueError("fullscreen must be a JSON boolean")
    # Do not accept arbitrary game assertions: the KWin producer supplies only
    # identity/fullscreen, and Deskflow applies local game metadata and rules.
    return {"version": 1, "updatedAtMs": stamp, "application": app,
            "fullscreen": state["fullscreen"] if app else False}


def write_state(directory, state):
    destination = directory / "deskflow-foreground-game.json"
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=directory,
                                         prefix=".deskflow-foreground-", delete=False) as output:
            temporary = Path(output.name)
            json.dump(state, output, separators=(",", ":"))
        os.replace(temporary, destination)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main():
    import dbus
    import dbus.service
    from dbus.mainloop.glib import DBusGMainLoop
    from gi.repository import GLib

    runtime = Path(os.environ.get("XDG_RUNTIME_DIR", ""))
    if not runtime.is_absolute() or not runtime.is_dir():
        raise RuntimeError("XDG_RUNTIME_DIR must be the current session's private runtime directory")
    permissions = runtime.stat()
    if permissions.st_uid != os.getuid() or stat.S_IMODE(permissions.st_mode) & 0o077:
        raise RuntimeError("runtime directory must be owned by this user and inaccessible to other users")
    DBusGMainLoop(set_as_default=True)
    bus = dbus.SessionBus()
    name = dbus.service.BusName("org.deskflow.ForegroundGame", bus=bus, do_not_queue=True)

    class Writer(dbus.service.Object):
        @dbus.service.method("org.deskflow.ForegroundGame", in_signature="s", out_signature="",
                             sender_keyword="sender")
        def Update(self, payload, sender=None):
            try:
                kwin = bus.get_name_owner("org.kde.KWin")
                if sender != kwin:
                    raise ValueError("only the current KWin process may publish foreground state")
                state = validate_state(str(payload), time.time_ns() // 1_000_000)
                write_state(runtime, state)
            except (ValueError, OSError) as error:
                raise dbus.exceptions.DBusException(str(error), name="org.deskflow.ForegroundGame.InvalidState")

    writer = Writer(name, "/org/deskflow/ForegroundGame")
    loop = GLib.MainLoop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, sig, lambda: (loop.quit(), False)[1])
    try:
        loop.run()
    finally:
        writer.remove_from_connection()
        (runtime / "deskflow-foreground-game.json").unlink(missing_ok=True)


if __name__ == "__main__":
    main()
