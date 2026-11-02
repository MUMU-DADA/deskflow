<!-- SPDX-FileCopyrightText: (C) 2026 Deskflow Developers -->
<!-- SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception -->

# System volume synchronization

On the **server**, open **Preferences → Volume sync** and enable **Synchronize system volume and mute**. The feature is off by default. Windows and macOS builds with this feature participate automatically; older peers keep sharing keyboard/mouse normally. It controls each computer's **current default playback output**, not microphones, per-application mixer sliders, audio streams, or the separate communications output on Windows.

Enter each computer's **equal-loudness reference**: the system volume percentage that gives the same perceived loudness. References range from 1 to 100; unspecified computers use 100. They are ratios, not a request to set those volumes during calibration. Changes are saved only when the Preferences dialog is accepted. The computer names must match the server layout; **Add computer** also supports names from an external server configuration.

For references **A=100, B=50**:

| Action | Result |
| --- | --- |
| A changes to 50 | B becomes 25 |
| B changes to 25 | A becomes 50 |
| B changes to 80 | A is limited to 100; B remains 80 |
| A toggles mute while B remains at 80 | Both mute states change; B's stored level remains 80 |

Each receiving level is `min(100, source level × receiver reference / source reference)`. The originating device is never corrected or echoed back. A receiver's rounded or limited result is not treated as a new local change. Volume and mute are separate components, so a mute-only operation cannot change the volume ratio after one receiver has hit its limit. Rapid updates are coalesced at roughly 100 ms; simultaneous edits are ordered as they reach the server.

**Initial connection, reconnect, or a client's default-output change** initializes that client from the server's actual current volume and mute state. Changing the server's default output uses that new output as the initial source for the clients. A disconnected client does not queue changes for later replay. Turning synchronization off leaves current volumes in place. Apply Preferences to restart the core and use a consistent snapshot of the switch and references.

The tab shows the local default output and whether its system volume/mute can be adjusted. Some outputs expose no software volume control; synchronization waits for a controllable default output. If an output supports volume but not mute, volume still synchronizes and the tab reports the mute limitation. It does not install an audio driver or capture/play audio.

## Settings and protocol

`core/syncSystemVolume` is the server's master switch (default `false`). `server/volumeWeights` is a JSON object mapping canonical computer names to integer references, for example `{"hostA":100,"hostB":50}`. Invalid reference data disables the feature. Clients ignore their own copy of these server settings.

The optional `VSCP=1` option negotiates `DVOL%1i%4i%4i%2i%1i`: kind, client endpoint generation, sequence, volume (0–10000), and flags. Kind `0` reports unavailable output; `1` advertises volume/mute capabilities; `2` sets a receiver; `3` reports local changes. Flags contain volume/mute component bits and the mute value. Every generation must first receive server initialization. Stale generations and duplicate/out-of-order sequences are ignored; malformed/unnegotiated messages are rejected. A client has at most one queued change event, preserving the latest volume and mute components.

Windows uses `IAudioEndpointVolume` on the default multimedia rendering endpoint. macOS uses Core Audio's current default output and the virtual-main volume control (with a writable master scalar fallback); virtual-main volume preserves the device's own channel/balance behavior. Native control is resolved against the current endpoint on each read/write. Successful write readback becomes the comparison baseline, so remote writes do not bounce between devices.

For read-only diagnostics, run `deskflow-core --volume-status`. `--volume-check` writes the current values back and verifies they remain unchanged; it exits before starting a core, input hooks, networking, or settings migration. These diagnostics are separate from normal keyboard-sharing operation.

API references: [Windows endpoint volume](https://learn.microsoft.com/en-us/windows/win32/api/endpointvolume/nn-endpointvolume-iaudioendpointvolume), [macOS virtual-main volume](https://developer.apple.com/documentation/audiotoolbox/kaudiohardwareservicedeviceproperty_virtualmainvolume).
