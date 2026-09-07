@echo off
REM Offline launcher for the EtherCAT Config Builder (Windows, no internet needed).
REM Runs the stdlib-only server directly -- no venv, no pip, no Node.
REM Requires only Python 3.x and a prebuilt frontend\dist folder.
cd /d "%~dp0"
where py >nul 2>nul && (py serve_offline.py %* & goto :eof)
where python >nul 2>nul && (python serve_offline.py %* & goto :eof)
echo Python 3.x is required. Install it from https://python.org and retry.
pause
