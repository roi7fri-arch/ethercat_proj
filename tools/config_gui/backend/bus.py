"""Bridge between the web backend and the EtherCAT segment.

The GUI runs on an engineer's laptop with a cable to the machine. Python cannot
open an EtherCAT raw socket in any reasonable way, and we do not want a second
implementation of the protocol anyway, so every bus operation is delegated to
src/tools/ecat_param_tool.c - the same binary an engineer can run by hand, and
one that links exactly the same master code as the production application.

The tool prints one JSON document on stdout and human-readable progress on
stderr, so both halves are captured and handed back to the UI.
"""

import json
import os
import shutil
import subprocess

REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))

TOOL_NAME = "ecat_param_tool"

# Where `make tools` puts it, host build first.
_SEARCH = [
    os.path.join(REPO_ROOT, "build", "host", TOOL_NAME),
    os.path.join(REPO_ROOT, "build", "aarch64", TOOL_NAME),
    os.path.join(REPO_ROOT, "build", "arm", TOOL_NAME),
]

# A bus operation should never hang the web server. Downloading a firmware image
# over FoE is the slow one, hence minutes rather than seconds.
DEFAULT_TIMEOUT_S = 300


class BusError(Exception):
    """Raised when the tool cannot be run at all (missing, no permission)."""


def tool_path():
    for p in _SEARCH:
        if os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    found = shutil.which(TOOL_NAME)
    return found


def _has_cap_net_raw(path):
    """True when the binary carries CAP_NET_RAW, so it can run unprivileged.

    Best-effort only: getcap may not be installed, in which case we simply do
    not claim either way.
    """
    getcap = shutil.which("getcap")
    if not getcap or not path:
        return None
    try:
        out = subprocess.run([getcap, path], capture_output=True, text=True,
                             timeout=5).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    return "cap_net_raw" in out


def status():
    """What the UI needs to tell the user whether downloading is possible."""
    path = tool_path()
    running_as_root = (os.geteuid() == 0) if hasattr(os, "geteuid") else False
    cap = _has_cap_net_raw(path)

    if path is None:
        hint = ("The bus tool has not been built. Run `make tools` in the "
                "repository root.")
    elif running_as_root or cap:
        hint = ""
    elif cap is False:
        hint = ("The tool needs raw-socket access. Grant it once with:\n"
                "  sudo setcap cap_net_raw,cap_net_admin+eip %s\n"
                "or start this server with sudo." % path)
    else:
        hint = ("If a bus operation fails with a permission error, grant the "
                "tool raw-socket access:\n"
                "  sudo setcap cap_net_raw,cap_net_admin+eip %s" % path)

    return {
        "available": path is not None,
        "path": path,
        "root": running_as_root,
        "cap_net_raw": cap,
        "hint": hint,
    }


def _run(args, timeout=DEFAULT_TIMEOUT_S):
    path = tool_path()
    if path is None:
        raise BusError("bus tool not built - run `make tools` in %s" % REPO_ROOT)

    cmd = [path] + args
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              timeout=timeout)
    except subprocess.TimeoutExpired:
        raise BusError("bus operation timed out after %ds: %s"
                       % (timeout, " ".join(cmd)))
    except OSError as exc:
        raise BusError("cannot run %s: %s" % (path, exc))

    # The tool always emits one JSON document, including for its own failures.
    payload = None
    text = (proc.stdout or "").strip()
    if text:
        try:
            payload = json.loads(text)
        except ValueError:
            payload = None

    if payload is None:
        payload = {
            "ok": False,
            "stage": "tool",
            "error": (proc.stderr or "").strip()
                     or "the tool produced no output (exit %d)" % proc.returncode,
        }

    payload["command"] = " ".join(cmd)
    payload["exit_code"] = proc.returncode
    payload["log"] = (proc.stderr or "").strip()
    return payload


def scan(iface, config_path=None):
    args = ["scan", "--iface", iface]
    if config_path:
        args += ["--config", config_path]
    return _run(args, timeout=60)


def download(iface, params_path, config_path=None, verify=True):
    args = ["download", "--iface", iface, "--params", params_path]
    if config_path:
        args += ["--config", config_path]
    if not verify:
        args += ["--no-verify"]
    return _run(args)


def upload(iface, params_path, out_path=None):
    args = ["upload", "--iface", iface, "--params", params_path]
    if out_path:
        args += ["--out", out_path]
    return _run(args)


def send_file(iface, slave, local_path, remote_name=None, password=0,
              use_boot=True):
    if not os.path.isfile(local_path):
        raise BusError("file not found: %s" % local_path)

    args = ["sendfile", "--iface", iface, "--slave", str(int(slave)),
            "--file", local_path, "--password", "0x%08X" % int(password)]
    if remote_name:
        args += ["--name", remote_name]
    if not use_boot:
        args += ["--no-boot"]
    return _run(args)


def send_files(iface, params_path):
    return _run(["files", "--iface", iface, "--params", params_path])
