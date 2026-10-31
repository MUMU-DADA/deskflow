#!/bin/bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
# The stable app and test build share IPC names. Never ask the existing process
# to stop automatically or allow a test GUI to attach to the stable core.
if pgrep -x Deskflow >/dev/null || pgrep -x deskflow-core >/dev/null; then
  echo "Quit the existing Deskflow application and core yourself before running the full test build."
  echo "For detection-only testing, use Run Game Probe.command; it can coexist with stable Deskflow."
  read -r -p "Press Return to close. " unused
  exit 1
fi
base="$HOME/Library/Application Support/Deskflow Game Guard Test"
export XDG_CONFIG_HOME="$base/config"
export XDG_STATE_HOME="$base/state"
mkdir -p "$XDG_CONFIG_HOME/Deskflow" "$XDG_STATE_HOME"
config="$XDG_CONFIG_HOME/Deskflow/Deskflow.conf"
if [ ! -e "$config" ]; then
  printf '[core]\nprocessMode=1\nenableFullscreenGameGuard=true\n[gui]\nstartCoreWithGui=false\n' > "$config"
fi
echo "Test configuration: $config"
exec "$here/Deskflow.app/Contents/MacOS/Deskflow"
