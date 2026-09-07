@echo off
REM Stop a running EtherCAT Config Builder started from this folder.
cd /d "%~dp0"
where py >nul 2>nul && (py run.py --stop & goto :eof)
python run.py --stop
