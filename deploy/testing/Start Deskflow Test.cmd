@echo off
rem SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
rem SPDX-License-Identifier: MIT
setlocal
cd /d "%~dp0"
tasklist /FI "IMAGENAME eq deskflow.exe" /NH | find /I "deskflow.exe" >nul && goto already_running
tasklist /FI "IMAGENAME eq deskflow-core.exe" /NH | find /I "deskflow-core.exe" >nul && goto already_running
set "XDG_STATE_HOME=%~dp0test-state"
"%~dp0deskflow.exe"
exit /b
:already_running
echo Quit existing Deskflow and its core yourself before running the full test build.
echo Use Run Game Probe.cmd for detection-only testing alongside stable Deskflow.
pause
exit /b 1
