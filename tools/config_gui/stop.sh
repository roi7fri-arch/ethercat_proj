#!/usr/bin/env bash
# Stop a running EtherCAT Config Builder started from this folder.
cd "$(dirname "$0")"
PY=$(command -v python3 || command -v python)
exec "$PY" run.py --stop
