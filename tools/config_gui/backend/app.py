"""FastAPI backend for the EtherCAT configuration builder.

Bus configuration (what the machine looks like):
  GET  /api/meta              -> modes, object dictionary, mode templates, defaults
  POST /api/config/validate   -> normalize + validate a config, return it
  GET  /api/config/load?path= -> read a JSON config from disk
  POST /api/config/save       -> write a JSON config to disk

Parameter sets (what the drives should be tuned to):
  GET  /api/params/meta       -> types, object catalogue, empty documents
  POST /api/params/validate   -> normalize + validate, plus commissioning advice
  GET  /api/params/load?path= -> read a parameter set from disk
  POST /api/params/save       -> write a parameter set to disk

Live bus (needs a cable to the machine; delegated to src/tools/ecat_param_tool):
  GET  /api/bus/status        -> is the tool built, does it have raw-socket rights
  POST /api/bus/scan          -> enumerate the slaves on a segment
  POST /api/bus/download      -> write a parameter set to the drives
  POST /api/bus/upload        -> read the drives' live values back
  POST /api/bus/sendfile      -> push one file over FoE
  POST /api/bus/sendfiles     -> push every file listed in a parameter set

In production the built React app is served from ../frontend/dist at "/".
"""

import json
import os
from typing import List, Optional

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field, field_validator

import bus
import meta
import param_meta

app = FastAPI(title="EtherCAT Config Builder")

# Vite dev server runs on a different port; allow it during development.
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


class PdoEntry(BaseModel):
    index: str
    subindex: int = Field(ge=0, le=255)
    bitlen: int = Field(gt=0, le=64)
    name: str = ""

    @field_validator("index")
    @classmethod
    def normalize_index(cls, v):
        v = v.strip()
        value = int(v, 16)
        if not 0 <= value <= 0xFFFF:
            raise ValueError("index out of range 0x0000-0xFFFF")
        return "0x%04X" % value


class Network(BaseModel):
    interface: str = "eth0"
    redundant_interface: str = ""
    cycle_time_us: int = Field(gt=0)
    number_of_cycles: int = Field(ge=0)
    distributed_clock: bool = True
    sync0_shift_us: int = 0
    sync_kp_div: int = Field(default=100, gt=0)
    sync_ki_div: int = Field(default=20, gt=0)
    auto_recovery: bool = True
    auto_recovery_timeout_us: int = Field(default=500, gt=0)
    verify_identity: bool = True


def _norm_index16(v):
    value = int(str(v).strip(), 16)
    if not 0 <= value <= 0xFFFF:
        raise ValueError("index out of range 0x0000-0xFFFF")
    return "0x%04X" % value


class SdoCmd(BaseModel):
    index: str
    subindex: int = Field(default=0, ge=0, le=255)
    size: int = Field(default=4)
    value: str = "0x00000000"
    comment: str = ""

    @field_validator("index")
    @classmethod
    def normalize_index(cls, v):
        return _norm_index16(v)

    @field_validator("size")
    @classmethod
    def check_size(cls, v):
        if v not in (1, 2, 4):
            raise ValueError("startup SDO size must be 1, 2 or 4 bytes")
        return v

    @field_validator("value")
    @classmethod
    def normalize_value(cls, v):
        return "0x%08X" % (int(str(v).strip(), 0) & 0xFFFFFFFF)


class Slave(BaseModel):
    position: Optional[int] = None
    name: str = ""
    profile: str = meta.DEFAULT_PROFILE
    mode_of_operation: int
    expected_vendor_id: Optional[str] = None
    expected_product_code: Optional[str] = None
    expected_revision: Optional[str] = None
    rxpdo_map_base: str = meta.MAP_DEFAULTS["rxpdo_map_base"]
    txpdo_map_base: str = meta.MAP_DEFAULTS["txpdo_map_base"]
    sm2_assign: str = meta.MAP_DEFAULTS["sm2_assign"]
    sm3_assign: str = meta.MAP_DEFAULTS["sm3_assign"]
    map_entries_per_obj: int = Field(default=meta.MAP_DEFAULTS["map_entries_per_obj"], gt=0, le=64)
    startup_sdo: List[SdoCmd] = []
    rxpdo: List[PdoEntry] = []
    txpdo: List[PdoEntry] = []

    @field_validator("rxpdo_map_base", "txpdo_map_base", "sm2_assign", "sm3_assign")
    @classmethod
    def normalize_map_index(cls, v):
        return _norm_index16(v)

    @field_validator("expected_vendor_id", "expected_product_code", "expected_revision")
    @classmethod
    def normalize_identity(cls, v):
        if v is None:
            return None
        s = str(v).strip()
        if s == "":
            return None
        return "0x%08X" % int(s, 0)   # accepts "0x..", decimal or int


class Config(BaseModel):
    version: int = meta.CONFIG_VERSION
    network: Network
    slaves: List[Slave]

    def renumbered(self):
        data = self.model_dump()
        for i, s in enumerate(data["slaves"]):
            s["position"] = i + 1
        return data


class SaveRequest(BaseModel):
    path: str
    config: Config


@app.get("/api/meta")
def get_meta():
    return {
        "version": meta.CONFIG_VERSION,
        "modes": meta.MODES,
        "object_dictionary": meta.OBJECT_DICTIONARY,
        "mode_templates": {str(k): v for k, v in meta.MODE_TEMPLATES.items()},
        "profiles": meta.PROFILES,
        "default_profile": meta.DEFAULT_PROFILE,
        "generic_profile": meta.GENERIC_PROFILE,
        "firmware_profiles": meta.FIRMWARE_PROFILES,
        "map_defaults": meta.MAP_DEFAULTS,
        "default_config": meta.default_config(),
    }


@app.post("/api/config/validate")
def validate_config(config: Config):
    data = config.renumbered()
    data["warnings"] = meta.profile_warnings(data)
    return data


@app.get("/api/config/load")
def load_config(path: str):
    path = os.path.expanduser(path)
    if not os.path.isfile(path):
        raise HTTPException(status_code=404, detail="file not found: %s" % path)
    try:
        with open(path, "r", encoding="utf-8") as fh:
            raw = json.load(fh)
    except (OSError, ValueError) as exc:
        raise HTTPException(status_code=400, detail="cannot read config: %s" % exc)
    try:
        return Config(**raw).renumbered()
    except Exception as exc:  # pydantic ValidationError -> 422 semantics
        raise HTTPException(status_code=422, detail="invalid config: %s" % exc)


@app.post("/api/config/save")
def save_config(req: SaveRequest):
    path = os.path.expanduser(req.path)
    parent = os.path.dirname(path) or "."
    if not os.path.isdir(parent):
        raise HTTPException(status_code=400, detail="directory does not exist: %s" % parent)
    data = req.config.renumbered()
    try:
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(data, fh, indent=2)
            fh.write("\n")
    except OSError as exc:
        raise HTTPException(status_code=400, detail="cannot write file: %s" % exc)
    return {"path": os.path.abspath(path), "bytes": os.path.getsize(path)}


# =========================================================================== #
# Parameter sets
#
# A separate document from the bus configuration: the config describes the bus
# and is read at every start-up, a parameter set is commissioning data written
# into the drives once. See backend/param_meta.py.
# =========================================================================== #

class ParamEntry(BaseModel):
    slave: int = Field(default=1, ge=0, le=255)   # 0 = every slave on the bus
    index: str
    subindex: int = Field(default=0, ge=0, le=255)
    type: str = param_meta.DEFAULT_PARAM_TYPE
    value: str = "0"
    verify: bool = True
    name: str = ""
    group: str = "General"
    unit: str = ""

    @field_validator("index")
    @classmethod
    def normalize_index(cls, v):
        return _norm_index16(v)

    @field_validator("type")
    @classmethod
    def check_type(cls, v):
        ids = [t["id"] for t in param_meta.PARAM_TYPES]
        if v not in ids:
            raise ValueError("unknown type '%s' (expected one of %s)"
                             % (v, ", ".join(ids)))
        return v

    @field_validator("value")
    @classmethod
    def strip_value(cls, v):
        v = str(v).strip()
        if v == "":
            raise ValueError("value must not be empty")
        return v


class ParamFile(BaseModel):
    slave: int = Field(default=1, ge=1, le=255)
    path: str = ""
    remote_name: str = ""
    password: str = "0x00000000"
    use_boot_state: bool = True

    @field_validator("password")
    @classmethod
    def normalize_password(cls, v):
        s = str(v).strip()
        if s == "":
            return "0x00000000"
        return "0x%08X" % (int(s, 0) & 0xFFFFFFFF)


class ParamSet(BaseModel):
    version: int = param_meta.PARAM_VERSION
    name: str = "parameter set"
    description: str = ""
    parameters: List[ParamEntry] = []
    files: List[ParamFile] = []


class ParamSaveRequest(BaseModel):
    path: str
    params: ParamSet


@app.get("/api/params/meta")
def get_param_meta():
    return {
        "version": param_meta.PARAM_VERSION,
        "types": param_meta.PARAM_TYPES,
        "default_type": param_meta.DEFAULT_PARAM_TYPE,
        "catalogue": param_meta.PARAM_CATALOGUE,
        "groups": param_meta.PARAM_GROUPS,
        "default_set": param_meta.default_param_set(),
        "default_entry": param_meta.default_param_entry(),
        "default_file": param_meta.default_param_file(),
    }


@app.post("/api/params/validate")
def validate_params(params: ParamSet):
    data = params.model_dump()
    data["warnings"] = param_meta.param_warnings(data)
    return data


@app.get("/api/params/load")
def load_params(path: str):
    path = os.path.expanduser(path)
    if not os.path.isfile(path):
        raise HTTPException(status_code=404, detail="file not found: %s" % path)
    try:
        with open(path, "r", encoding="utf-8") as fh:
            raw = json.load(fh)
    except (OSError, ValueError) as exc:
        raise HTTPException(status_code=400,
                            detail="cannot read parameter set: %s" % exc)
    try:
        return ParamSet(**raw).model_dump()
    except Exception as exc:
        raise HTTPException(status_code=422,
                            detail="invalid parameter set: %s" % exc)


@app.post("/api/params/save")
def save_params(req: ParamSaveRequest):
    path = os.path.expanduser(req.path)
    parent = os.path.dirname(path) or "."
    if not os.path.isdir(parent):
        raise HTTPException(status_code=400,
                            detail="directory does not exist: %s" % parent)
    try:
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(req.params.model_dump(), fh, indent=2)
            fh.write("\n")
    except OSError as exc:
        raise HTTPException(status_code=400, detail="cannot write file: %s" % exc)
    return {"path": os.path.abspath(path), "bytes": os.path.getsize(path)}


# =========================================================================== #
# Bus operations
#
# Everything here shells out to src/tools/ecat_param_tool.c - see backend/bus.py
# for why. These endpoints touch real hardware, so each one takes an explicit
# interface name and none of them has a default.
# =========================================================================== #

class ScanRequest(BaseModel):
    interface: str
    config_path: Optional[str] = None


class DownloadRequest(BaseModel):
    interface: str
    params_path: str
    config_path: Optional[str] = None
    verify: bool = True


class UploadRequest(BaseModel):
    interface: str
    params_path: str
    out_path: Optional[str] = None


class SendFileRequest(BaseModel):
    interface: str
    slave: int = Field(default=1, ge=1, le=255)
    path: str
    remote_name: Optional[str] = None
    password: str = "0x00000000"
    use_boot_state: bool = True


class SendFilesRequest(BaseModel):
    interface: str
    params_path: str


def _bus_call(fn, *args, **kwargs):
    try:
        return fn(*args, **kwargs)
    except bus.BusError as exc:
        raise HTTPException(status_code=503, detail=str(exc))


@app.get("/api/bus/status")
def bus_status():
    """Whether a bus operation is possible at all, and what to do if not."""
    return bus.status()


@app.post("/api/bus/scan")
def bus_scan(req: ScanRequest):
    return _bus_call(bus.scan, req.interface, req.config_path)


@app.post("/api/bus/download")
def bus_download(req: DownloadRequest):
    return _bus_call(bus.download, req.interface, req.params_path,
                     req.config_path, req.verify)


@app.post("/api/bus/upload")
def bus_upload(req: UploadRequest):
    return _bus_call(bus.upload, req.interface, req.params_path, req.out_path)


@app.post("/api/bus/sendfile")
def bus_sendfile(req: SendFileRequest):
    return _bus_call(bus.send_file, req.interface, req.slave, req.path,
                     req.remote_name, int(req.password, 0), req.use_boot_state)


@app.post("/api/bus/sendfiles")
def bus_sendfiles(req: SendFilesRequest):
    return _bus_call(bus.send_files, req.interface, req.params_path)


# Serve the built SPA if it exists (production single-process mode).
_dist = os.path.join(os.path.dirname(__file__), "..", "frontend", "dist")
if os.path.isdir(_dist):
    app.mount("/", StaticFiles(directory=_dist, html=True), name="spa")
