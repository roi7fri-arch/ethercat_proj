"""Shared EtherCAT configuration metadata.

Single source of truth for the modes of operation, the known-object picklist,
and the ready-made per-mode PDO templates. Exposed to the frontend via /api/meta
so the UI never duplicates this data.
"""

CONFIG_VERSION = 1

# CiA402 modes of operation (value written to object 0x6060).
MODES = [
    {"value": 1, "label": "PP  - Profile Position"},
    {"value": 3, "label": "PV  - Profile Velocity"},
    {"value": 4, "label": "TQ  - Profile Torque"},
    {"value": 6, "label": "HM  - Homing"},
    {"value": 8, "label": "CSP - Cyclic Sync Position"},
    {"value": 9, "label": "CSV - Cyclic Sync Velocity"},
    {"value": 10, "label": "CST - Cyclic Sync Torque"},
]


def _obj(index, sub, bits, name):
    return {"index": index, "subindex": sub, "bitlen": bits, "name": name}


# Known CoE objects offered as picklist presets, grouped by typical direction.
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

# Ready-made PDO maps per mode, matching the existing cyclic_sync_*_mode.dat files.
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


# --- Drive profiles -------------------------------------------------------
# The tool is vendor-agnostic: a profile bundles the object picklist and the
# per-mode PDO templates that suit a given drive family. Elmo profiles expose
# the manufacturer objects; ACS / generic profiles are restricted to the
# standard CiA402 dictionary. The device's own SII/ESI still drives per-slave
# SM sizing at runtime, so a wrong guess here is only a picklist inconvenience.

def _is_standard_index(index_str):
    idx = int(index_str, 16)
    return (0x1000 <= idx <= 0x1FFF) or (0x6000 <= idx <= 0x9FFF)


def _std_only(entries):
    return [dict(e) for e in entries if _is_standard_index(e["index"])]


_STD_OBJECTS = {"rx": _std_only(OBJECT_DICTIONARY["rx"]),
                "tx": _std_only(OBJECT_DICTIONARY["tx"])}
_STD_TEMPLATES = {
    m: {"rx": _std_only(t["rx"]), "tx": _std_only(t["tx"])}
    for m, t in MODE_TEMPLATES.items()
}

DEFAULT_PROFILE = "elmo_platinum"

# The id of the fallback the firmware uses for any profile it does not know.
# Must match drive_profile_generic()->id in src/drive/drive_profile.c.
GENERIC_PROFILE = "cia402_generic"

# Families the master has a registered drive_profile_t for (src/vendors/).
# Anything else is still perfectly usable - the firmware falls back to the
# generic CiA 402 handling - but it gets no vendor quirks or fault decoding,
# so the UI says so rather than letting the user assume otherwise.
#
# Keep these ids identical to the `id` fields in src/vendors/*/*.c. The
# multivendor simulation test is what proves an unlisted family still works.
FIRMWARE_PROFILES = ["elmo_platinum", "elmo_gold", GENERIC_PROFILE]

PROFILES = [
    {
        "id": "elmo_platinum",
        "label": "Elmo Platinum",
        "description": "Single or dual axis. Clears stale mapping objects on "
                       "start-up; manufacturer fault codes decoded.",
        "default_vendor_id": "0x0000009A",
        "default_product_code": "0x00030924",
        "object_dictionary": OBJECT_DICTIONARY,
        "mode_templates": MODE_TEMPLATES,
    },
    {
        "id": "elmo_gold",
        "label": "Elmo Gold",
        "description": "Standard CiA 402 behaviour plus Elmo fault decoding.",
        "default_vendor_id": "0x0000009A",
        "default_product_code": "0x00030924",
        "object_dictionary": OBJECT_DICTIONARY,
        "mode_templates": MODE_TEMPLATES,
    },
    {
        "id": GENERIC_PROFILE,
        "label": "Generic CiA 402",
        "description": "Any standards-compliant drive. No vendor extensions.",
        "default_vendor_id": "",
        "default_product_code": "",
        "object_dictionary": _STD_OBJECTS,
        "mode_templates": _STD_TEMPLATES,
    },
    {
        "id": "acs",
        "label": "ACS Motion Control",
        "description": "Standard CiA 402 picklist. Fill the identity fields "
                       "from the drive's ESI file.",
        "default_vendor_id": "",
        "default_product_code": "",
        "object_dictionary": _STD_OBJECTS,
        "mode_templates": _STD_TEMPLATES,
    },
    {
        "id": "copley_accelnet",
        "label": "Copley Accelnet / Xenus",
        "description": "Standard CiA 402 picklist. Fill the identity fields "
                       "from the drive's ESI file.",
        "default_vendor_id": "",
        "default_product_code": "",
        "object_dictionary": _STD_OBJECTS,
        "mode_templates": _STD_TEMPLATES,
    },
    {
        "id": "maxon_epos4",
        "label": "maxon EPOS4",
        "description": "Standard CiA 402 picklist. Fill the identity fields "
                       "from the drive's ESI file.",
        "default_vendor_id": "",
        "default_product_code": "",
        "object_dictionary": _STD_OBJECTS,
        "mode_templates": _STD_TEMPLATES,
    },
]

# Annotate each entry with whether the firmware has a dedicated profile for it,
# so the frontend does not have to know the rule.
for _p in PROFILES:
    _p["firmware"] = _p["id"] in FIRMWARE_PROFILES
    _p["falls_back_to"] = None if _p["firmware"] else GENERIC_PROFILE


def profile_by_id(profile_id):
    for p in PROFILES:
        if p["id"] == profile_id:
            return p
    return None


def profile_warnings(config):
    """Warnings about the drive families a configuration names.

    Not errors: an unrecognised profile still runs, it just gets the generic
    CiA 402 treatment. Surfacing it here stops a typo in the profile name from
    silently costing a drive its vendor quirks.
    """
    out = []
    for slave in config.get("slaves", []):
        pos = slave.get("position")
        pid = (slave.get("profile") or "").strip()
        name = slave.get("name") or "slave %s" % pos

        if not pid:
            out.append("slave %s (%s): no profile set, the master will use %s"
                       % (pos, name, GENERIC_PROFILE))
            continue

        known = profile_by_id(pid)
        if known is None:
            out.append("slave %s (%s): profile '%s' is unknown to this tool; "
                       "the master will fall back to %s"
                       % (pos, name, pid, GENERIC_PROFILE))
        elif not known["firmware"]:
            out.append("slave %s (%s): '%s' has no dedicated driver in the "
                       "master; it will run as %s (standard CiA 402, no vendor "
                       "quirks or fault decoding)"
                       % (pos, name, known["label"], GENERIC_PROFILE))

        if known is not None and known["firmware"]:
            expected = slave.get("expected_vendor_id")
            default = known.get("default_vendor_id")
            if default and expected and int(expected, 0) != int(default, 0):
                out.append("slave %s (%s): vendor id %s does not match the "
                           "usual %s for %s"
                           % (pos, name, expected, default, known["label"]))
    return out

# Default PDO-mapping / SM-assignment objects (standard CiA402 addresses).
MAP_DEFAULTS = {
    "rxpdo_map_base": "0x1600",
    "txpdo_map_base": "0x1A00",
    "sm2_assign": "0x1C12",
    "sm3_assign": "0x1C13",
    "map_entries_per_obj": 8,
}


def default_slave(position=1, profile=DEFAULT_PROFILE, name="Elmo Platinum",
                  mode=8):
    return {
        "position": position,
        "name": name,
        "profile": profile,
        "mode_of_operation": mode,
        "rxpdo_map_base": MAP_DEFAULTS["rxpdo_map_base"],
        "txpdo_map_base": MAP_DEFAULTS["txpdo_map_base"],
        "sm2_assign": MAP_DEFAULTS["sm2_assign"],
        "sm3_assign": MAP_DEFAULTS["sm3_assign"],
        "map_entries_per_obj": MAP_DEFAULTS["map_entries_per_obj"],
        "startup_sdo": [],
        "rxpdo": list(MODE_TEMPLATES[mode]["rx"]),
        "txpdo": list(MODE_TEMPLATES[mode]["tx"]),
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
            "verify_identity": True,
        },
        "slaves": [default_slave()],
    }
