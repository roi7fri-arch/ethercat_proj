@echo off
REM Start the EtherCAT Config Builder (Windows). Double-click or run from cmd.
cd /d "%~dp0"
where py >nul 2>nul && (py run.py %* & goto :eof)
where python >nul 2>nul && (python run.py %* & goto :eof)
echo Python 3.8+ is required. Install it from https://python.org and retry.
pause
