# EtherCAT Config Builder

Desktop web app that generates the JSON configuration the EtherCAT master reads
on init (network settings, each slave's mode of operation, and the RxPDO/TxPDO
object maps).

## Requirements

- **Python 3.8+** (always required)
- **Node.js 18+** (required only to *build* the UI; not needed if a prebuilt
  `frontend/dist/` is shipped with the folder)

## Start / stop

| Platform | Start | Stop |
|----------|-------|------|
| Linux / macOS | `./start.sh` | `Ctrl+C`, or `./stop.sh` from another terminal |
| Windows | double-click `start.bat` | close the window, or run `stop.bat` |

The first start automatically creates the Python virtual environment, installs
all dependencies, builds the React app, then serves it on
<http://localhost:8000> and opens your browser. Later starts skip setup and just
launch.

## Options

```
./start.sh                # build if needed, serve on :8000, open browser
./start.sh --port 9000    # use a different port
./start.sh --no-browser   # don't auto-open the browser
./start.sh --dev          # developer mode with Vite hot reload (frontend :5173)
./start.sh --rebuild      # force reinstall deps and rebuild the UI
```

## Install on another computer

1. Copy the whole `config_gui/` folder (or `git clone` the repo).
2. Ensure Python 3.8+ is installed (and Node 18+ if the folder has no
   `frontend/dist/`).
3. Run `./start.sh` (Linux/macOS) or `start.bat` (Windows). Everything else is
   automatic.

To ship a **Node-free** package, run `./start.sh --rebuild` once on a machine
that has Node, then copy the folder *including* `frontend/dist/`. Target
machines then need only Python.
