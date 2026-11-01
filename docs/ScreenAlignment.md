<!-- SPDX-FileCopyrightText: (C) 2026 Deskflow Developers -->
<!-- SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception -->

# Screen alignment

Screen alignment limits mouse crossing to the physical overlap between two adjacent computer desktops. Configure it on the server in **Server Configuration → Computers → Screen alignment…**. First place the two computers next to each other in the layout.

1. Run the new GUI and core on both computers and establish their Deskflow connection. Choose the adjacent connection, enable **Use physical screen alignment for this connection**, then click **Calibrate on the actual screens…**.
2. Blue **A** and orange **B** arrows appear on each real screen's adjoining edge. Drag the two A arrow tips into physical alignment across the seam, then do the same for B. Keep A and B well apart on each screen. Match the letters as well as the colors.
3. Optionally enable the green **C** marker and align that pair to check the remaining error. C checks the result; it does not change the calibration calculated from A and B.
4. Check the green overlap in the preview. The result describes relative edge lengths and offset, not absolute millimeters. Only the shared portion permits automatic edge crossing, in both directions.
5. Click **Use calibration**, then **Save** in Screen alignment and **Save** in the outer Server Configuration dialog. The calibration is only applied after both saves; canceling the outer dialog discards the draft.

Right-click a marker, press **Escape**, or choose **Cancel** to stop a calibration session. A timeout or lost connection also cancels it and removes the markers. These cancellations preserve the previous configuration.

Real-screen markers support Windows, macOS, and X11 with one monitor per computer. For Wayland or multiple monitors, use **Use manual dimensions** instead: enter each desktop's visible width and height in millimeters, check any starting estimates, and swap width and height for portrait orientation as needed. Drag the second preview rectangle along the seam, enter an offset, or use the alignment presets. A preview with no overlap cannot be saved. Save both dialogs to apply manual alignment too.

Window managers and system panels can restrict which windows stay on top. If a taskbar, menu, or panel obscures an arrow tip, cancel the session and use manual dimensions rather than estimating the hidden point.

To restore the usual whole-edge crossing, disable alignment for the connection or choose **Restore whole edges**, then save both dialogs. This does not change the game's automatic crossing protection.

Alignment is available for the built-in layout, not an external configuration file. Calibration is tied to computer names and the connection's direction. Renaming, removing, reversing, or moving computers so that the connection changes can clear it; check the alignment after changing the layout.

Each rectangle represents one computer's entire desktop. This editor does not arrange individual monitors or their internal edges within a computer. Alignment controls where the pointer crosses between computers; it does not calibrate pointer speed.
