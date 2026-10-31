Deskflow fullscreen game guard - prebuilt test package

No compiler, Qt SDK, Homebrew, Python, GitHub CLI or other development tools are
needed on the test computer. Qt and other runtime libraries are bundled.
SOURCE-COMMIT.txt identifies the source used to build this package.

Start with the detector-only probe
---------------------------------
The probe observes the current foreground window and writes NDJSON diagnostics.
It does NOT capture keyboard/mouse input, start a Deskflow server/client, contact
another computer, or load/change your existing Deskflow settings.

macOS (Apple Silicon / arm64):
1. Extract the .tar.gz using Archive Utility into a NEW folder. Keep both .app
   bundles intact. Do not overwrite /Applications/Deskflow.app.
2. Double-click Run Game Probe.command. It runs the separate Deskflow Game Probe
   app for about 60 seconds and saves results to Desktop/Deskflow Game Probe Results.
3. Switch into your game, use borderless fullscreen/fullscreen, then switch away.
4. The probe checks Accessibility permission without prompting or granting it.
   If results report that permission is missing, a human must add Deskflow Game
   Probe.app in System Settings > Privacy & Security > Accessibility, then rerun.
   An unavailable detector is not protection.

Windows (x64):
1. Extract the .zip into a NEW writable folder, keeping all DLLs and plugins.
2. Double-click Run Game Probe.cmd. Results appear in its probe-results folder.
3. Enter fullscreen in your game and switch away during the recording period.

The probe reports fullscreen/game/supported/guard state independently. A game
that cannot be identified automatically needs an exact application rule. The
prebuilt probe supports --game-app, --samples, --interval-ms and --output; --help
lists them. No code edits or compilation are needed to use those options.

Unsigned test build and macOS permissions
----------------------------------------
These are development test packages, not a signed/notarized public release.
macOS app bundles have ad-hoc signatures, which verify file integrity but do not
identify a trusted Apple developer. Gatekeeper may block the first launch. Only
the human user should decide whether to allow this exact downloaded build via
System Settings > Privacy & Security > Open Anyway. Do not disable Gatekeeper
globally. Updating this test build may require renewing its Accessibility grant.
The .command launchers may separately require the usual user-approved Open action.

Full two-computer test (after the probe result is correct)
-------------------------------------------------------
Use the matching fork build on BOTH computers to exercise client-reported game
state. Keep the current stable installation; do not replace it with this package.
Quit the stable GUI/core yourself before starting the full test build. The test
app shares IPC names with stable Deskflow and is not intended to run concurrently.
The supplied launcher refuses to start if a GUI/core is already running.

Always use Start Deskflow Test.command / Start Deskflow Test.cmd for the full app.
These launchers isolate test settings and runtime state. Launching the main
Deskflow app directly on macOS would use the normal settings path instead.
macOS test config: ~/Library/Application Support/Deskflow Game Guard Test/
Windows test config: settings/Deskflow.conf and test-state/ inside this folder.
The package does not register a Windows service or change startup settings.
Set up the test pair manually; existing connection/settings files are not copied.
macOS may ask the human user for Accessibility and Local Network permission for
the test app/core. TLS trust and certificate confirmation also remain explicit.

Expected behavior: only the currently controlled computer's foreground fullscreen
game inhibits mouse edge crossing. Switching away from the game restores normal
crossing; explicit screen-switch hotkeys remain available. Recognition and desktop
API limitations are documented in the source docs/FullscreenGameGuard.md.

SHA256
------
A .sha256 file is provided next to the archive. macOS can verify it with built-in
shasum; Windows can verify it with PowerShell Get-FileHash. The checksum detects
download corruption, but is not a publisher signature.
