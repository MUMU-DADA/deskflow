<!-- SPDX-FileCopyrightText: (C) 2026 Deskflow Developers -->
<!-- SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception -->

# Screen alignment

Screen alignment limits mouse crossing to the physical overlap between two adjacent computer desktops. Configure it on the server in **Server Configuration → Computers → Screen alignment…**. First place the two computers next to each other in the layout.

1. Choose the connection and enable **Use physical screen alignment for this connection**.
2. Enter each display's visible width and height in millimeters, excluding the frame. Enter remote dimensions manually and check any starting estimates. For a portrait display, use **Swap width and height** if needed so the values match its current orientation.
3. Drag the second rectangle along the shared edge, enter the offset, or use the alignment presets. Side-by-side desktops move up and down; desktops arranged above and below one another move left and right.
4. Check the green overlap in the preview. Only that shared portion permits automatic edge crossing, in both directions. If there is no overlap, **Save** is disabled; adjust the offset or choose **Restore whole edges** before saving.
5. Click **Save** inside Screen alignment, then **Save** in the outer Server Configuration dialog to apply the change. Canceling the outer dialog discards the draft.

To restore the usual whole-edge crossing, disable alignment for the connection or choose **Restore whole edges**, then save both dialogs. This does not change the game's automatic crossing protection.

Alignment is available for the built-in layout, not an external configuration file. Calibration is tied to computer names and the connection's direction. Renaming, removing, reversing, or moving computers so that the connection changes can clear it; check the alignment after changing the layout.

Each rectangle represents one computer's entire desktop. This editor does not arrange individual monitors within a computer. Dimensions are manual measurements, and the feature controls where the pointer crosses between computers; it does not calibrate pointer speed.
