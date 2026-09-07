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
