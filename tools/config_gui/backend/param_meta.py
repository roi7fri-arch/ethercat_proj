"""Metadata for drive parameter sets.

A parameter set is a separate document from the bus configuration:

  ethercat_config.json   what the bus looks like - PDO maps, modes, identity.
                         Deployment data; the master reads it every start-up.
  <name>.params.json     what the drives should be tuned to - gains, limits,
                         scaling, homing. Commissioning data; written into the
                         drives' own non-volatile memory once, from this GUI.

This module is the picklist the GUI offers when adding a parameter, plus the
shape of the document. The C side (src/params/param_set.c) is the authority on
what is actually accepted - everything here is convenience.
"""

PARAM_VERSION = 1

# Data types, matching param_type_t in src/params/param_set.h.
PARAM_TYPES = [
    {"id": "u8",  "label": "u8  - unsigned 8 bit",  "bytes": 1},
    {"id": "i8",  "label": "i8  - signed 8 bit",    "bytes": 1},
    {"id": "u16", "label": "u16 - unsigned 16 bit", "bytes": 2},
    {"id": "i16", "label": "i16 - signed 16 bit",   "bytes": 2},
    {"id": "u32", "label": "u32 - unsigned 32 bit", "bytes": 4},
    {"id": "i32", "label": "i32 - signed 32 bit",   "bytes": 4},
    {"id": "f32", "label": "f32 - IEEE-754 float",  "bytes": 4},
]

DEFAULT_PARAM_TYPE = "u32"


def _p(index, sub, ptype, name, group, unit="", value="0"):
    return {
        "index": index,
        "subindex": sub,
        "type": ptype,
        "name": name,
        "group": group,
        "unit": unit,
        "value": value,
    }


# Standard CiA 402 objects that are actually worth tuning, grouped the way a
# commissioning engineer thinks about them. Offered as a picklist; the engineer
# can always type an arbitrary index instead.
PARAM_CATALOGUE = [
    # --- Limits -----------------------------------------------------------
    _p("0x6072", 0, "u16", "Max torque",            "Limits", "per mille", "1000"),
    _p("0x6073", 0, "u16", "Max current",           "Limits", "per mille", "1000"),
    _p("0x6075", 0, "u32", "Motor rated current",   "Limits", "mA",        "0"),
    _p("0x6076", 0, "u32", "Motor rated torque",    "Limits", "mNm",       "0"),
    _p("0x607D", 1, "i32", "Min position limit",    "Limits", "counts",    "0"),
    _p("0x607D", 2, "i32", "Max position limit",    "Limits", "counts",    "0"),
    _p("0x607F", 0, "u32", "Max profile velocity",  "Limits", "counts/s",  "0"),
    _p("0x6080", 0, "u32", "Max motor speed",       "Limits", "rpm",       "0"),

    # --- Profile ----------------------------------------------------------
    _p("0x6081", 0, "u32", "Profile velocity",      "Profile", "counts/s", "0"),
    _p("0x6083", 0, "u32", "Profile acceleration",  "Profile", "counts/s²", "0"),
    _p("0x6084", 0, "u32", "Profile deceleration",  "Profile", "counts/s²", "0"),
    _p("0x6085", 0, "u32", "Quick stop deceleration", "Profile", "counts/s²", "0"),
    _p("0x6086", 0, "i16", "Motion profile type",   "Profile", "",          "0"),
    _p("0x60C5", 0, "u32", "Max acceleration",      "Profile", "counts/s²", "0"),
    _p("0x60C6", 0, "u32", "Max deceleration",      "Profile", "counts/s²", "0"),

    # --- Homing -----------------------------------------------------------
    _p("0x607C", 0, "i32", "Home offset",           "Homing", "counts",    "0"),
    _p("0x6098", 0, "i8",  "Homing method",         "Homing", "",          "0"),
    _p("0x6099", 1, "u32", "Homing speed (search)", "Homing", "counts/s",  "0"),
    _p("0x6099", 2, "u32", "Homing speed (zero)",   "Homing", "counts/s",  "0"),
    _p("0x609A", 0, "u32", "Homing acceleration",   "Homing", "counts/s²", "0"),

    # --- Control loop -----------------------------------------------------
    _p("0x6065", 0, "u32", "Following error window", "Control", "counts",  "0"),
    _p("0x6066", 0, "u16", "Following error timeout", "Control", "ms",     "0"),
    _p("0x6067", 0, "u32", "Position window",       "Control", "counts",   "0"),
    _p("0x6068", 0, "u16", "Position window time",  "Control", "ms",       "0"),
    _p("0x60F2", 0, "u16", "Positioning option code", "Control", "",       "0"),

    # --- Scaling / feedback ----------------------------------------------
    _p("0x608F", 1, "u32", "Encoder increments",    "Scaling", "counts",   "0"),
    _p("0x608F", 2, "u32", "Motor revolutions",     "Scaling", "rev",      "1"),
    _p("0x6091", 1, "u32", "Gear motor revolutions", "Scaling", "rev",     "1"),
    _p("0x6091", 2, "u32", "Gear shaft revolutions", "Scaling", "rev",     "1"),
    _p("0x6092", 1, "u32", "Feed constant feed",    "Scaling", "counts",   "0"),
    _p("0x6092", 2, "u32", "Feed constant rev",     "Scaling", "rev",      "1"),

    # --- Behaviour --------------------------------------------------------
    _p("0x605A", 0, "i16", "Quick stop option code",     "Behaviour", "", "2"),
    _p("0x605B", 0, "i16", "Shutdown option code",       "Behaviour", "", "0"),
    _p("0x605C", 0, "i16", "Disable operation option",   "Behaviour", "", "1"),
    _p("0x605D", 0, "i16", "Halt option code",           "Behaviour", "", "1"),
    _p("0x605E", 0, "i16", "Fault reaction option code", "Behaviour", "", "2"),
    _p("0x6060", 0, "i8",  "Modes of operation",         "Behaviour", "", "8"),

    # --- Persistence ------------------------------------------------------
    # 0x1010:01 = "save" - writing the ASCII signature "save" (0x65766173)
    # commits the drive's current parameters to non-volatile memory. Without it
    # everything written here is lost at the next power cycle, which is the
    # single most common commissioning surprise.
    _p("0x1010", 1, "u32", "Store parameters (write 0x65766173)",
       "Persistence", "signature", "0x65766173"),
    _p("0x1011", 1, "u32", "Restore defaults (write 0x64616F6C)",
       "Persistence", "signature", "0x64616F6C"),
]

PARAM_GROUPS = []
for _entry in PARAM_CATALOGUE:
    if _entry["group"] not in PARAM_GROUPS:
        PARAM_GROUPS.append(_entry["group"])


def default_param_entry(slave=1):
    return {
        "slave": slave,
        "index": "0x6072",
        "subindex": 0,
        "type": "u16",
        "value": "1000",
        "verify": True,
        "name": "Max torque",
        "group": "Limits",
        "unit": "per mille",
    }


def default_param_set():
    return {
        "version": PARAM_VERSION,
        "name": "New parameter set",
        "description": "",
        "parameters": [],
        "files": [],
    }


def default_param_file(slave=1):
    return {
        "slave": slave,
        "path": "",
        "remote_name": "",
        "password": "0x00000000",
        "use_boot_state": True,
    }


def param_warnings(param_set):
    """Advice that is worth showing before someone presses Download.

    None of these are errors - they are the things that waste an afternoon.
    """
    out = []
    params = param_set.get("parameters", [])

    if not params and not param_set.get("files"):
        out.append("This set is empty: nothing would be written.")

    # Duplicates: the later write silently wins, which is rarely intended.
    seen = {}
    for p in params:
        key = (p.get("slave"), p.get("index"), p.get("subindex"))
        if key in seen:
            out.append("0x%s:%s appears more than once for slave %s - "
                       "the last value wins"
                       % (str(p.get("index")).replace("0x", ""),
                          p.get("subindex"), p.get("slave")))
        seen[key] = True

    # The classic: tuning written, never committed, gone at the next power-up.
    has_store = any(str(p.get("index", "")).lower() == "0x1010" for p in params)
    if params and not has_store:
        out.append("No 'Store parameters' (0x1010:01) entry: the values will "
                   "be active now but lost when the drive is power-cycled.")

    # A restore that runs after the tuning would undo it.
    indices = [str(p.get("index", "")).lower() for p in params]
    if "0x1011" in indices and "0x1010" in indices:
        if indices.index("0x1011") > indices.index("0x1010"):
            out.append("'Restore defaults' (0x1011) is ordered after 'Store "
                       "parameters' (0x1010) - that would undo the set.")

    for f in param_set.get("files", []):
        if not f.get("path"):
            out.append("A file entry has no path and will be skipped.")
        if f.get("use_boot_state"):
            out.append("Slave %s: '%s' is sent in BOOT state. The drive stops "
                       "controlling the motor during the transfer - make sure "
                       "the axis is safe." % (f.get("slave"), f.get("path")))

    return out
