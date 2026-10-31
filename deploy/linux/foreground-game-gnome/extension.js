/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Shell from 'gi://Shell';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

export default class ForegroundGameGuard extends Extension {
    enable() {
        this._file = Gio.File.new_for_path(
            GLib.build_filenamev([GLib.get_user_runtime_dir(), 'deskflow-foreground-game.json']));
        this._writeState();
        this._timer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 250, () => {
            this._writeState();
            return GLib.SOURCE_CONTINUE;
        });
    }

    _writeState() {
        try {
            const state = {
                version: 1,
                updatedAtMs: Date.now(),
                application: '',
                fullscreen: false,
                game: false,
            };
            const window = global.display.focus_window;
            if (window && !window.minimized && window.showing_on_its_workspace()) {
                const app = Shell.WindowTracker.get_default().get_window_app(window);
                state.application = (app?.get_id() ?? window.get_wm_class() ?? '').replace(/\.desktop$/, '');
                const categories = app?.get_app_info()?.get_categories() ?? '';
                state.game = categories.split(';').includes('Game') ||
                    /^steam_app_[1-9][0-9]*$/.test(window.get_wm_class() ?? '');
                state.fullscreen = window.is_fullscreen();
                // Also accept an undecorated game window filling its monitor.
                const monitor = window.get_monitor();
                if (!state.fullscreen && !window.decorated && monitor >= 0) {
                    const bounds = window.get_frame_rect();
                    const screen = global.display.get_monitor_geometry(monitor);
                    state.fullscreen = bounds.x === screen.x && bounds.y === screen.y &&
                        bounds.width === screen.width && bounds.height === screen.height;
                }
            }
            this._file.replace_contents(JSON.stringify(state), null, false,
                Gio.FileCreateFlags.PRIVATE | Gio.FileCreateFlags.REPLACE_DESTINATION, null);
            this._reportedError = false;
        } catch (error) {
            // Do not refresh old data on failure. Deskflow rejects stale files.
            if (!this._reportedError) {
                console.error(`Deskflow foreground state: ${error.message}`);
                this._reportedError = true;
            }
        }
    }

    disable() {
        if (this._timer) {
            GLib.Source.remove(this._timer);
            this._timer = null;
        }
        try {
            this._file?.delete(null);
        } catch (_) {
            // The file may already have been removed when the session ended.
        }
        this._file = null;
        this._reportedError = false;
    }
}
