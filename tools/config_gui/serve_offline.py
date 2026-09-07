#!/usr/bin/env python3
"""Offline, dependency-free launcher for the EtherCAT Config Builder React UI.

Serves the prebuilt frontend/dist and the same /api endpoints as the FastAPI
backend, but using only the Python standard library so it runs on the stock
Python 3.6 shipped with Ubuntu 18.04 -- no pip, no FastAPI, no pydantic, no
internet. Requires only that frontend/dist and backend/meta.py are present.

Usage:
    python3 serve_offline.py                 # serve on http://localhost:8000
    python3 serve_offline.py --port 9000
    python3 serve_offline.py --no-browser
"""

import argparse
import json
import os
import sys
import threading
import types
import webbrowser
from http.server import BaseHTTPRequestHandler, HTTPServer
from socketserver import ThreadingMixIn
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))


def _load_meta():
    """Use backend/meta.py when present; otherwise fall back to the embedded copy
    below so this script also works when copied on its own."""
    for cand in (os.path.join(HERE, "backend"), HERE):
        if os.path.isfile(os.path.join(cand, "meta.py")):
            sys.path.insert(0, cand)
            try:
                import meta as _m  # noqa: E402
                return _m
            except ImportError:
                pass
    return _embedded_meta()


def _embedded_meta():
    def _obj(index, sub, bits, name):
        return {"index": index, "subindex": sub, "bitlen": bits, "name": name}

    CONFIG_VERSION = 1
    MODES = [
        {"value": 1, "label": "PP  - Profile Position"},
        {"value": 3, "label": "PV  - Profile Velocity"},
        {"value": 4, "label": "TQ  - Profile Torque"},
        {"value": 6, "label": "HM  - Homing"},
        {"value": 8, "label": "CSP - Cyclic Sync Position"},
        {"value": 9, "label": "CSV - Cyclic Sync Velocity"},
        {"value": 10, "label": "CST - Cyclic Sync Torque"},
    ]
    OBJECT_DICTIONARY = {
        "rx": [
            _obj("0x6040", 0, 16, "Controlword"),
            _obj("0x6060", 0, 8, "Modes of operation"),
            _obj("0x607A", 0, 32, "Target position"),
            _obj("0x60FF", 0, 32, "Target velocity"),
            _obj("0x6071", 0, 16, "Target torque"),
            _obj("0x6072", 0, 16, "Max torque"),
            _obj("0x60B0", 0, 32, "Position offset"),
            _obj("0x60B1", 0, 32, "Velocity offset"),
            _obj("0x60B2", 0, 16, "Torque offset"),
        ],
        "tx": [
            _obj("0x6041", 0, 16, "Statusword"),
            _obj("0x6061", 0, 8, "Modes of operation display"),
            _obj("0x603F", 0, 16, "Error code"),
            _obj("0x6062", 0, 32, "Position demand value"),
            _obj("0x6064", 0, 32, "Position actual value"),
            _obj("0x606B", 0, 32, "Velocity demand value"),
            _obj("0x606C", 0, 32, "Velocity actual value"),
            _obj("0x6074", 0, 16, "Torque demand"),
            _obj("0x6077", 0, 16, "Torque actual value"),
            _obj("0x6078", 0, 16, "Current actual value"),
            _obj("0x60F4", 0, 32, "Following error actual value"),
            _obj("0x3610", 0, 32, "Elmo: manufacturer object"),
            _obj("0x3640", 0, 32, "Elmo: manufacturer object"),
            _obj("0x2FE4", 1, 64, "Elmo: manufacturer object"),
            _obj("0x2FE8", 1, 32, "Elmo: manufacturer object"),
            _obj("0x2FEC", 1, 32, "Elmo: manufacturer object"),
            _obj("0xF6F0", 1, 32, "Elmo: manufacturer object"),
        ],
    }
    MODE_TEMPLATES = {
        8: {
            "rx": [
                _obj("0x6040", 0, 16, "Controlword"),
                _obj("0x60B0", 0, 32, "Position offset"),
                _obj("0x60B1", 0, 32, "Velocity offset"),
                _obj("0x60B2", 0, 16, "Torque offset"),
            ],
            "tx": [
                _obj("0x6041", 0, 16, "Statusword"),
                _obj("0x6061", 0, 8, "Modes of operation display"),
                _obj("0x603F", 0, 16, "Error code"),
                _obj("0x6062", 0, 32, "Position demand value"),
                _obj("0x606B", 0, 32, "Velocity demand value"),
                _obj("0x6074", 0, 16, "Torque demand"),
                _obj("0x6064", 0, 32, "Position actual value"),
                _obj("0x606C", 0, 32, "Velocity actual value"),
                _obj("0x6077", 0, 16, "Torque actual value"),
                _obj("0x6078", 0, 16, "Current actual value"),
                _obj("0x60F4", 0, 32, "Following error actual value"),
                _obj("0x3610", 0, 32, "Elmo: manufacturer object"),
                _obj("0x3640", 0, 32, "Elmo: manufacturer object"),
            ],
        },
        9: {
            "rx": [
                _obj("0x6040", 0, 16, "Controlword"),
                _obj("0x60B1", 0, 32, "Velocity offset"),
                _obj("0x60B2", 0, 16, "Torque offset"),
                _obj("0x60FF", 0, 32, "Target velocity"),
            ],
            "tx": [
                _obj("0x6041", 0, 16, "Statusword"),
                _obj("0x6061", 0, 8, "Modes of operation display"),
                _obj("0x606B", 0, 32, "Velocity demand value"),
                _obj("0x6064", 0, 32, "Position actual value"),
                _obj("0x606C", 0, 32, "Velocity actual value"),
                _obj("0x6077", 0, 16, "Torque actual value"),
                _obj("0x6078", 0, 16, "Current actual value"),
                _obj("0x2FE4", 1, 64, "Elmo: manufacturer object"),
                _obj("0x2FE8", 1, 32, "Elmo: manufacturer object"),
                _obj("0x2FE4", 2, 64, "Elmo: manufacturer object"),
                _obj("0x2FE8", 2, 32, "Elmo: manufacturer object"),
                _obj("0x2FE4", 3, 64, "Elmo: manufacturer object"),
                _obj("0x2FE8", 3, 32, "Elmo: manufacturer object"),
                _obj("0x2FEC", 1, 32, "Elmo: manufacturer object"),
                _obj("0x3640", 0, 32, "Elmo: manufacturer object"),
                _obj("0x603F", 0, 16, "Error code"),
                _obj("0xF6F0", 1, 32, "Elmo: manufacturer object"),
            ],
        },
        10: {
            "rx": [
                _obj("0x6040", 0, 16, "Controlword"),
                _obj("0x60B2", 0, 16, "Torque offset"),
                _obj("0x6071", 0, 16, "Target torque"),
            ],
            "tx": [
                _obj("0x6041", 0, 16, "Statusword"),
                _obj("0x6061", 0, 8, "Modes of operation display"),
                _obj("0x6074", 0, 16, "Torque demand"),
                _obj("0x6064", 0, 32, "Position actual value"),
                _obj("0x606C", 0, 32, "Velocity actual value"),
                _obj("0x6077", 0, 16, "Torque actual value"),
                _obj("0x6078", 0, 16, "Current actual value"),
                _obj("0x2FE4", 1, 64, "Elmo: manufacturer object"),
                _obj("0x2FE8", 1, 32, "Elmo: manufacturer object"),
                _obj("0x2FE4", 2, 64, "Elmo: manufacturer object"),
                _obj("0x2FE8", 2, 32, "Elmo: manufacturer object"),
                _obj("0x2FE4", 3, 64, "Elmo: manufacturer object"),
                _obj("0x2FE8", 3, 32, "Elmo: manufacturer object"),
                _obj("0x2FEC", 1, 32, "Elmo: manufacturer object"),
                _obj("0x3640", 0, 32, "Elmo: manufacturer object"),
                _obj("0x603F", 0, 16, "Error code"),
                _obj("0xF6F0", 1, 32, "Elmo: manufacturer object"),
            ],
        },
    }

    def default_config():
        return {
            "version": CONFIG_VERSION,
            "network": {
                "interface": "eth0",
                "redundant_interface": "",
                "cycle_time_us": 250,
                "number_of_cycles": 12000,
                "distributed_clock": True,
                "sync0_shift_us": 0,
                "sync_kp_div": 100,
                "sync_ki_div": 20,
                "auto_recovery": True,
                "auto_recovery_timeout_us": 500,
            },
            "slaves": [
                {
                    "position": 1,
                    "name": "Elmo Platinum",
                    "mode_of_operation": 8,
                    "rxpdo": list(MODE_TEMPLATES[8]["rx"]),
                    "txpdo": list(MODE_TEMPLATES[8]["tx"]),
                }
            ],
        }

    return types.SimpleNamespace(
        CONFIG_VERSION=CONFIG_VERSION,
        MODES=MODES,
        OBJECT_DICTIONARY=OBJECT_DICTIONARY,
        MODE_TEMPLATES=MODE_TEMPLATES,
        default_config=default_config,
    )


meta = _load_meta()

DIST = os.path.join(HERE, "frontend", "dist")

CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".js": "application/javascript",
    ".mjs": "application/javascript",
    ".css": "text/css",
    ".json": "application/json",
    ".map": "application/json",
    ".svg": "image/svg+xml",
    ".ico": "image/x-icon",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".woff": "font/woff",
    ".woff2": "font/woff2",
    ".ttf": "font/ttf",
}


class ApiError(Exception):
    def __init__(self, status, detail):
        super().__init__(detail)
        self.status = status
        self.detail = detail


# ---- validation / normalization (mirrors the pydantic models) -----------
def _require_int(obj, key, default=None, minv=None, maxv=None, exclusive_min=False):
    if key not in obj or obj[key] is None:
        if default is None:
            raise ApiError(422, "missing field: %s" % key)
        return default
    try:
        val = int(obj[key])
    except (TypeError, ValueError):
        raise ApiError(422, "%s must be an integer" % key)
    if minv is not None and (val <= minv if exclusive_min else val < minv):
        raise ApiError(422, "%s out of range" % key)
    if maxv is not None and val > maxv:
        raise ApiError(422, "%s out of range" % key)
    return val


def _norm_index(v):
    try:
        value = int(str(v).strip(), 16)
    except (TypeError, ValueError):
        raise ApiError(422, "invalid index: %r" % v)
    if not 0 <= value <= 0xFFFF:
        raise ApiError(422, "index out of range 0x0000-0xFFFF")
    return "0x%04X" % value


def _norm_identity(v):
    """Optional 32-bit id -> '0x%08X', or None when blank/absent (skip check)."""
    if v is None:
        return None
    s = str(v).strip()
    if s == "":
        return None
    try:
        value = int(s, 0)   # accepts 0x.., decimal
    except ValueError:
        raise ApiError(422, "invalid identity value: %r" % v)
    return "0x%08X" % value


def _norm_pdo(e):
    if not isinstance(e, dict):
        raise ApiError(422, "pdo entry must be an object")
    return {
        "index": _norm_index(e.get("index")),
        "subindex": _require_int(e, "subindex", minv=0, maxv=255),
        "bitlen": _require_int(e, "bitlen", minv=0, maxv=64, exclusive_min=True),
        "name": str(e.get("name", "") or ""),
    }


def _norm_network(n):
    if not isinstance(n, dict):
        raise ApiError(422, "network must be an object")
    return {
        "interface": str(n.get("interface", "eth0") or "eth0"),
        "redundant_interface": str(n.get("redundant_interface", "") or ""),
        "cycle_time_us": _require_int(n, "cycle_time_us", minv=0, exclusive_min=True),
        "number_of_cycles": _require_int(n, "number_of_cycles", minv=0),
        "distributed_clock": bool(n.get("distributed_clock", True)),
        "sync0_shift_us": _require_int(n, "sync0_shift_us", default=0),
        "sync_kp_div": _require_int(n, "sync_kp_div", default=100, minv=0, exclusive_min=True),
        "sync_ki_div": _require_int(n, "sync_ki_div", default=20, minv=0, exclusive_min=True),
        "auto_recovery": bool(n.get("auto_recovery", True)),
        "auto_recovery_timeout_us": _require_int(n, "auto_recovery_timeout_us", default=500, minv=0, exclusive_min=True),
    }


def _norm_config(raw):
    if not isinstance(raw, dict):
        raise ApiError(422, "config must be an object")
    slaves_in = raw.get("slaves")
    if not isinstance(slaves_in, list):
        raise ApiError(422, "config.slaves must be a list")
    slaves = []
    for i, s in enumerate(slaves_in):
        if not isinstance(s, dict):
            raise ApiError(422, "slave must be an object")
        slaves.append({
            "position": i + 1,  # renumbered
            "name": str(s.get("name", "") or ""),
            "mode_of_operation": _require_int(s, "mode_of_operation"),
            "expected_vendor_id": _norm_identity(s.get("expected_vendor_id")),
            "expected_product_code": _norm_identity(s.get("expected_product_code")),
            "expected_revision": _norm_identity(s.get("expected_revision")),
            "rxpdo": [_norm_pdo(e) for e in s.get("rxpdo", []) or []],
            "txpdo": [_norm_pdo(e) for e in s.get("txpdo", []) or []],
        })
    return {
        "version": _require_int(raw, "version", default=meta.CONFIG_VERSION),
        "network": _norm_network(raw.get("network")),
        "slaves": slaves,
    }


# ---- request handler ----------------------------------------------------
class Handler(BaseHTTPRequestHandler):
    server_version = "EtherCATConfigOffline/1.0"

    def log_message(self, fmt, *args):
        sys.stderr.write("  %s - %s\n" % (self.address_string(), fmt % args))

    def _send_json(self, obj, status=200):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json_body(self):
        length = int(self.headers.get("Content-Length", 0) or 0)
        raw = self.rfile.read(length) if length else b""
        try:
            return json.loads(raw.decode("utf-8") or "{}")
        except ValueError as exc:
            raise ApiError(400, "invalid JSON body: %s" % exc)

    # -- GET: /api/meta, /api/config/load, or static files --
    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path
        try:
            if path == "/api/meta":
                return self._send_json({
                    "version": meta.CONFIG_VERSION,
                    "modes": meta.MODES,
                    "object_dictionary": meta.OBJECT_DICTIONARY,
                    "mode_templates": {str(k): v for k, v in meta.MODE_TEMPLATES.items()},
                    "default_config": meta.default_config(),
                })
            if path == "/api/config/load":
                qs = parse_qs(parsed.query)
                fpath = (qs.get("path", [""])[0] or "").strip()
                return self._send_json(self._load_config(fpath))
            if path.startswith("/api/"):
                raise ApiError(404, "unknown endpoint")
            return self._serve_static(path)
        except ApiError as exc:
            self._send_json({"detail": exc.detail}, status=exc.status)

    # -- POST: /api/config/validate, /api/config/save --
    def do_POST(self):
        path = urlparse(self.path).path
        try:
            if path == "/api/config/validate":
                body = self._read_json_body()
                return self._send_json(_norm_config(body))
            if path == "/api/config/save":
                body = self._read_json_body()
                return self._send_json(self._save_config(body))
            raise ApiError(404, "unknown endpoint")
        except ApiError as exc:
            self._send_json({"detail": exc.detail}, status=exc.status)

    def _load_config(self, fpath):
        fpath = os.path.expanduser(fpath)
        if not fpath or not os.path.isfile(fpath):
            raise ApiError(404, "file not found: %s" % fpath)
        try:
            with open(fpath, "r", encoding="utf-8") as fh:
                raw = json.load(fh)
        except (OSError, ValueError) as exc:
            raise ApiError(400, "cannot read config: %s" % exc)
        return _norm_config(raw)

    def _save_config(self, body):
        if not isinstance(body, dict) or "path" not in body or "config" not in body:
            raise ApiError(400, "save request needs 'path' and 'config'")
        path = os.path.expanduser(str(body["path"]))
        parent = os.path.dirname(path) or "."
        if not os.path.isdir(parent):
            raise ApiError(400, "directory does not exist: %s" % parent)
        data = _norm_config(body["config"])
        try:
            with open(path, "w", encoding="utf-8") as fh:
                json.dump(data, fh, indent=2)
                fh.write("\n")
        except OSError as exc:
            raise ApiError(400, "cannot write file: %s" % exc)
        return {"path": os.path.abspath(path), "bytes": os.path.getsize(path)}

    def _serve_static(self, path):
        if not os.path.isdir(DIST):
            raise ApiError(500, "frontend/dist not found -- ship the prebuilt UI")
        rel = path.lstrip("/") or "index.html"
        full = os.path.normpath(os.path.join(DIST, rel))
        # prevent path traversal outside dist; SPA fallback to index.html
        if not full.startswith(os.path.abspath(DIST)) or not os.path.isfile(full):
            full = os.path.join(DIST, "index.html")
        with open(full, "rb") as fh:
            data = fh.read()
        ext = os.path.splitext(full)[1].lower()
        self.send_response(200)
        self.send_header("Content-Type", CONTENT_TYPES.get(ext, "application/octet-stream"))
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


class ThreadingHTTPServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True


def main():
    parser = argparse.ArgumentParser(description="Offline EtherCAT Config Builder (stdlib only)")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()

    if not os.path.isdir(DIST):
        sys.stderr.write("ERROR: %s missing. This package must include the prebuilt React UI.\n" % DIST)
        sys.exit(1)

    url = "http://%s:%d/" % ("localhost" if args.host in ("0.0.0.0", "127.0.0.1") else args.host, args.port)
    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    print("EtherCAT Config Builder (offline) serving %s" % url)
    print("Press Ctrl+C to stop.")
    if not args.no_browser:
        threading.Timer(0.8, lambda: webbrowser.open(url)).start()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped.")
        httpd.server_close()


if __name__ == "__main__":
    main()
