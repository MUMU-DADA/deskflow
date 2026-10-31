/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

import QtQuick
import org.kde.kwin as KWinComponents

Item {
    id: root
    property bool pending: false

    function report() {
        // Only one async D-Bus request in flight if the helper is unavailable.
        if (pending)
            return;
        const state = {
            version: 1,
            updatedAtMs: Date.now(),
            application: "",
            fullscreen: false
        };
        const window = KWinComponents.Workspace.activeWindow;
        if (window && window.active && !window.minimized && window.normalWindow) {
            state.application = String(window.desktopFileName || window.resourceClass || "").replace(/\.desktop$/, "");
            state.fullscreen = window.fullScreen;
            if (!state.fullscreen && window.noBorder && window.output) {
                const bounds = window.frameGeometry;
                const screen = window.output.geometry;
                state.fullscreen = bounds.x === screen.x && bounds.y === screen.y && bounds.width === screen.width && bounds.height === screen.height;
            }
        }
        // Game identity comes from Deskflow's desktop catalogue/application rules.
        writer.arguments = [JSON.stringify(state)];
        pending = true;
        writer.call();
    }

    KWinComponents.DBusCall {
        id: writer
        service: "org.deskflow.ForegroundGame"
        path: "/org/deskflow/ForegroundGame"
        dbusInterface: "org.deskflow.ForegroundGame"
        method: "Update"
        onFinished: root.pending = false
        onFailed: root.pending = false
    }

    Timer {
        interval: 250
        repeat: true
        running: true
        triggeredOnStart: true
        onTriggered: root.report()
    }
}
