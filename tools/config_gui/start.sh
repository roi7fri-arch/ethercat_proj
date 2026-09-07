#!/usr/bin/env bash
# Start the EtherCAT Config Builder (Linux / macOS).
# First run auto-installs everything, then opens your browser.
set -e
cd "$(dirname "$0")"
PY=$(command -v python3 || command -v python)
if [ -z "$PY" ]; then
  echo "Python 3.8+ is required. Install it and retry." >&2
  exit 1
fi
exec "$PY" run.py "$@"
