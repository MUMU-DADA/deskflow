@echo off
rem SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
rem SPDX-License-Identifier: MIT
setlocal
cd /d "%~dp0"
if not exist "probe-results" mkdir "probe-results"
set "PROBE_RESULT=%~dp0probe-results\probe-%RANDOM%-%RANDOM%.ndjson"
echo This probe only observes the foreground app. It does not capture or forward input.
echo Switch to your game, enter fullscreen, then switch away during the next 60 seconds.
echo Results: %PROBE_RESULT%
"%~dp0deskflow-game-probe.exe" --samples 120 --interval-ms 500 --output "%PROBE_RESULT%"
echo Finished. Results: %PROBE_RESULT%
pause
