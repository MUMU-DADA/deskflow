#!/bin/bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
results="$HOME/Desktop/Deskflow Game Probe Results"
mkdir -p "$results"
output="$results/probe-$(date +%Y%m%d-%H%M%S)-$$.ndjson"
echo "This probe only observes the foreground application. It does not capture or forward input."
echo "Switch to your game, enter fullscreen, then switch away during the next 60 seconds."
echo "If results report missing Accessibility permission, manually add Deskflow Game Probe in System Settings, then rerun."
echo "Results: $output"
open -g -W -n "$here/Deskflow Game Probe.app" --args --samples 120 --interval-ms 500 --output "$output"
echo "Finished. Results: $output"
read -r -p "Press Return to close. " unused
