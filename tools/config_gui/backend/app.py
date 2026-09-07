"""FastAPI backend for the EtherCAT configuration builder.

Endpoints:
  GET  /api/meta              -> modes, object dictionary, mode templates, defaults
  POST /api/config/validate   -> normalize + validate a config, return it
  GET  /api/config/load?path= -> read a JSON config from disk
  POST /api/config/save       -> write a JSON config to disk

In production the built React app is served from ../frontend/dist at "/".
"""

import json
import os
from typing import List, Optional

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field, field_validator

import meta

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
        "map_defaults": meta.MAP_DEFAULTS,
        "default_config": meta.default_config(),
    }


@app.post("/api/config/validate")
def validate_config(config: Config):
    return config.renumbered()


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


# Serve the built SPA if it exists (production single-process mode).
_dist = os.path.join(os.path.dirname(__file__), "..", "frontend", "dist")
if os.path.isdir(_dist):
    app.mount("/", StaticFiles(directory=_dist, html=True), name="spa")
