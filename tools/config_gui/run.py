#!/usr/bin/env python3
"""One-command launcher for the EtherCAT Config Builder (Linux, macOS, Windows).

First run bootstraps everything automatically:
  * creates the backend Python virtual environment and installs its deps,
  * installs the frontend npm packages and builds the React app,
then serves the built app + API from a single port and opens your browser.

Usage:
  python3 run.py                # build (if needed) and serve on http://localhost:8000
  python3 run.py --dev          # developer mode: Vite hot-reload (frontend :5173)
  python3 run.py --port 9000    # serve on a different port
  python3 run.py --no-browser   # do not auto-open the browser
  python3 run.py --rebuild      # force reinstall deps and rebuild the frontend
  python3 run.py --stop         # stop a running instance started from this folder
"""

import argparse
import os
import shutil
import signal
import subprocess
import sys
import threading
import time
import webbrowser
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BACKEND = ROOT / "backend"
FRONTEND = ROOT / "frontend"
VENV = BACKEND / ".venv"
DIST = FRONTEND / "dist"
PIDFILE = ROOT / ".gui.pid"
IS_WIN = os.name == "nt"


def venv_python():
    return VENV / ("Scripts/python.exe" if IS_WIN else "bin/python")


def log(msg):
    print("[ethercat-gui] " + msg, flush=True)


def die(msg):
    print("[ethercat-gui] ERROR: " + msg, file=sys.stderr, flush=True)
    sys.exit(1)


# ---- bootstrap (runs under the system interpreter) ----------------------
def ensure_backend(force):
    if not VENV.exists():
        log("creating Python virtual environment...")
        subprocess.check_call([sys.executable, "-m", "venv", str(VENV)])
        force = True
    if force:
        log("installing backend dependencies...")
        subprocess.check_call([str(venv_python()), "-m", "pip", "install", "-q",
                               "--upgrade", "pip"])
        subprocess.check_call([str(venv_python()), "-m", "pip", "install", "-q",
                               "-r", str(BACKEND / "requirements.txt")])


def relaunch_in_venv():
    """Re-exec this script using the venv interpreter so imports resolve."""
    env = dict(os.environ, ECAT_IN_VENV="1")
    os.execve(str(venv_python()), [str(venv_python()), __file__, *sys.argv[1:]], env)


# ---- frontend build -----------------------------------------------------
def npm_cmd():
    exe = shutil.which("npm.cmd") if IS_WIN else shutil.which("npm")
    return exe


def ensure_frontend(force):
    if DIST.exists() and not force:
        return
    npm = npm_cmd()
    if not npm:
        if DIST.exists():
            return
        die("Node.js/npm not found. Install Node 18+ (https://nodejs.org) and retry.")
    if not (FRONTEND / "node_modules").exists() or force:
        log("installing frontend packages (first run may take a minute)...")
        subprocess.check_call([npm, "install"], cwd=str(FRONTEND))
    log("building the React app...")
    subprocess.check_call([npm, "run", "build"], cwd=str(FRONTEND))


# ---- stop ---------------------------------------------------------------
def stop_running():
    if not PIDFILE.exists():
        log("no running instance found.")
        return
    pid = int(PIDFILE.read_text().strip() or "0")
    try:
        if IS_WIN:
            subprocess.run(["taskkill", "/PID", str(pid), "/F", "/T"], check=False)
        else:
            os.kill(pid, signal.SIGTERM)
        log("stopped instance (pid %d)." % pid)
    except (ProcessLookupError, OSError):
        log("instance not running; clearing stale pid file.")
    finally:
        PIDFILE.unlink(missing_ok=True)


def write_pidfile():
    PIDFILE.write_text(str(os.getpid()))

    def cleanup(*_):
        PIDFILE.unlink(missing_ok=True)
        sys.exit(0)

    signal.signal(signal.SIGTERM, cleanup)
    return cleanup


# ---- serve --------------------------------------------------------------
def open_browser_later(url):
    def go():
        time.sleep(1.5)
        webbrowser.open(url)
    threading.Thread(target=go, daemon=True).start()


def serve_prod(host, port, open_browser):
    sys.path.insert(0, str(BACKEND))
    os.chdir(BACKEND)
    import uvicorn
    import app as backend

    url = "http://localhost:%d/" % port
    log("serving on %s   (press Ctrl+C to stop)" % url)
    if open_browser:
        open_browser_later(url)
    uvicorn.run(backend.app, host=host, port=port, log_level="warning")


def serve_dev(host, port, open_browser):
    npm = npm_cmd()
    if not npm:
        die("--dev needs Node.js/npm installed.")
    sys.path.insert(0, str(BACKEND))
    os.chdir(BACKEND)
    import uvicorn
    import app as backend

    log("starting Vite dev server (hot reload) on http://localhost:5173/ ...")
    vite = subprocess.Popen([npm, "run", "dev"], cwd=str(FRONTEND))
    url = "http://localhost:5173/"
    log("frontend %s  |  api http://localhost:%d/api  (press Ctrl+C to stop)" % (url, port))
    if open_browser:
        open_browser_later(url)
    try:
        uvicorn.run(backend.app, host=host, port=port, log_level="warning")
    finally:
        vite.terminate()
        try:
            vite.wait(timeout=5)
        except subprocess.TimeoutExpired:
            vite.kill()


# ---- main ---------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(add_help=True, description="EtherCAT Config Builder launcher")
    parser.add_argument("--dev", action="store_true", help="developer mode with hot reload")
    parser.add_argument("--port", type=int, default=8000, help="API/app port (default 8000)")
    parser.add_argument("--host", default="127.0.0.1", help="bind host (default 127.0.0.1)")
    parser.add_argument("--no-browser", action="store_true", help="do not open the browser")
    parser.add_argument("--rebuild", action="store_true", help="reinstall deps and rebuild")
    parser.add_argument("--stop", action="store_true", help="stop a running instance")
    args = parser.parse_args()

    if args.stop:
        stop_running()
        return

    if os.environ.get("ECAT_IN_VENV") != "1":
        if sys.version_info < (3, 8):
            die("Python 3.8+ is required.")
        ensure_backend(args.rebuild)
        relaunch_in_venv()  # does not return

    # From here we run inside the venv interpreter.
    try:
        import fastapi  # noqa: F401
    except ImportError:
        ensure_backend(True)

    if not args.dev:
        ensure_frontend(args.rebuild)

    write_pidfile()
    open_browser = not args.no_browser
    try:
        if args.dev:
            serve_dev(args.host, args.port, open_browser)
        else:
            serve_prod(args.host, args.port, open_browser)
    except KeyboardInterrupt:
        pass
    finally:
        PIDFILE.unlink(missing_ok=True)
        log("stopped.")


if __name__ == "__main__":
    main()
