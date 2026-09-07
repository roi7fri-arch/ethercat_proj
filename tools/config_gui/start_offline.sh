#!/bin/sh
# Offline launcher for the EtherCAT Config Builder (Linux/macOS, no internet needed).
# Runs the stdlib-only server directly -- no venv, no pip, no Node.
# Requires only Python 3.x and a prebuilt frontend/dist folder.
cd "$(dirname "$0")" || exit 1
if command -v python3 >/dev/null 2>&1; then exec python3 serve_offline.py "$@"; fi
if command -v python  >/dev/null 2>&1; then exec python  serve_offline.py "$@"; fi
echo "Python 3.x is required. Install it and retry." >&2
exit 1
