#!/bin/bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$repo/build}"
out="${2:-$repo/dist-test}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
stage="$out/deskflow-game-guard-macos-arm64"
if [ -e "$stage" ]; then
  echo "Refusing to overwrite existing package staging directory: $stage" >&2
  exit 1
fi
mkdir -p "$stage"
ditto "$build/bin/Deskflow.app" "$stage/Deskflow.app"
ditto "$build/bin/deskflow-game-probe.app" "$stage/Deskflow Game Probe.app"

# Change only the staged full-test bundle, before deployment/signing. Its
# executable and folder names remain compatible with Deskflow's core lookup.
# The stable installed application retains its own bundle/TCC identity.
python3 -B - "$stage/Deskflow.app/Contents/Info.plist" <<'PY'
import pathlib
import plistlib
import sys

path = pathlib.Path(sys.argv[1])
with path.open("rb") as stream:
    info = plistlib.load(stream)
if info.get("CFBundleExecutable") != "Deskflow":
    raise RuntimeError("Unexpected full-test bundle executable")
info["CFBundleIdentifier"] = "org.deskflow.deskflow.gameguard-test"
info["CFBundleName"] = "Deskflow Game Guard Test"
info["CFBundleDisplayName"] = "Deskflow Game Guard Test"
with path.open("wb") as stream:
    plistlib.dump(info, stream, sort_keys=False)
PY

# BUILD_INSTALLER=OFF skips deploy/mac/deploy.cmake entirely. Deploy both apps,
# including the core executable nested inside the GUI bundle, explicitly here.
macdeployqt "$stage/Deskflow.app" "-executable=$stage/Deskflow.app/Contents/MacOS/deskflow-core" -always-overwrite -verbose=2
macdeployqt "$stage/Deskflow Game Probe.app" -always-overwrite -verbose=2

cp "$repo/deploy/testing/README.txt" "$stage/README.txt"
cp "$repo/deploy/testing/Run Game Probe.command" "$stage/Run Game Probe.command"
cp "$repo/deploy/testing/Start Deskflow Test.command" "$stage/Start Deskflow Test.command"
cp "$repo/LICENSE" "$stage/LICENSE"
ditto "$repo/LICENSES" "$stage/LICENSES"
printf '%s\n' "$(git -C "$repo" rev-parse HEAD)" > "$stage/SOURCE-COMMIT.txt"
chmod +x "$stage/Run Game Probe.command" "$stage/Start Deskflow Test.command"

# Never use development entitlements or claim notarization. Sign nested code
# after all deployment mutations, then verify signatures and dependency closure.
for app in "$stage/Deskflow.app" "$stage/Deskflow Game Probe.app"; do
  codesign --force --deep --sign - "$app"
  codesign --verify --deep --strict "$app"
done
python3 -B "$repo/deploy/testing/test_verify_macos_bundle.py"
python3 -B "$repo/deploy/testing/verify_macos_bundle.py" "$stage/Deskflow.app" "$stage/Deskflow Game Probe.app"

# A relocated, minimal-environment smoke check must load only bundled Qt.
# --help / --version return before input hooks, networking or Settings startup.
smoke="$out/smoke-home"
mkdir -p "$smoke/config" "$smoke/state"
env -i HOME="$smoke" PATH=/usr/bin:/bin XDG_CONFIG_HOME="$smoke/config" XDG_STATE_HOME="$smoke/state" \
  QT_QPA_PLATFORM=cocoa "$stage/Deskflow.app/Contents/MacOS/deskflow-core" --version
env -i HOME="$smoke" PATH=/usr/bin:/bin \
  "$stage/Deskflow Game Probe.app/Contents/MacOS/deskflow-game-probe" --help
env -i HOME="$smoke" PATH=/usr/bin:/bin \
  "$stage/Deskflow Game Probe.app/Contents/MacOS/deskflow-game-probe" --samples 1 \
  --output "$out/probe-smoke.ndjson"
test -s "$out/probe-smoke.ndjson"

archive="$(basename "$stage").tar.gz"
tar -C "$out" -czf "$out/$archive" "$(basename "$stage")"
(cd "$out" && shasum -a 256 "$archive" > "$archive.sha256")
echo "Created $out/$archive"
