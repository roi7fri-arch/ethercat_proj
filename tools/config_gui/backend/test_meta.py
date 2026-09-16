"""Consistency checks for the configuration GUI metadata.

Run with:  python3 tools/config_gui/backend/test_meta.py

The important one is test_firmware_profiles_match_the_master: the GUI's idea of
which drive families have a dedicated driver is duplicated from the C registry,
and duplicated data drifts. This reads the profile ids straight out of
src/vendors/ and fails if the two ever disagree.
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import meta  # noqa: E402
import param_meta  # noqa: E402

REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
VENDORS_DIR = os.path.join(REPO_ROOT, "src", "vendors")
DRIVE_PROFILE_C = os.path.join(REPO_ROOT, "src", "drive", "drive_profile.c")

_FAILURES = []


def check(cond, msg):
    if cond:
        print("  [PASS] %s" % msg)
    else:
        print("  [FAIL] %s" % msg)
        _FAILURES.append(msg)


def _profile_ids_in_c():
    """Ids of every drive_profile_t defined under src/vendors/, plus the
    built-in generic profile from src/drive/drive_profile.c."""
    ids = set()

    pattern = re.compile(
        r"drive_profile_t\s+\w+\s*=\s*\{\s*\"([A-Za-z0-9_]+)\"", re.S)

    for root, _dirs, files in os.walk(VENDORS_DIR):
        for fn in files:
            if not fn.endswith(".c"):
                continue
            with open(os.path.join(root, fn), "r", encoding="utf-8") as fh:
                ids.update(pattern.findall(fh.read()))

    with open(DRIVE_PROFILE_C, "r", encoding="utf-8") as fh:
        ids.update(pattern.findall(fh.read()))

    return ids


def test_firmware_profiles_match_the_master():
    print("TEST firmware profile list matches src/vendors")

    in_c = _profile_ids_in_c()
    in_gui = set(meta.FIRMWARE_PROFILES)

    check(bool(in_c), "found profile definitions in the C sources (%d)" % len(in_c))
    check(meta.GENERIC_PROFILE in in_c,
          "generic profile id '%s' exists in C" % meta.GENERIC_PROFILE)

    missing = in_c - in_gui
    extra = in_gui - in_c
    check(not missing,
          "every C profile is listed in FIRMWARE_PROFILES (missing: %s)"
          % (sorted(missing) or "none"))
    check(not extra,
          "FIRMWARE_PROFILES claims nothing the master lacks (extra: %s)"
          % (sorted(extra) or "none"))


def test_every_firmware_profile_is_offered():
    print("TEST the picker offers every implemented family")

    offered = {p["id"] for p in meta.PROFILES}
    for pid in meta.FIRMWARE_PROFILES:
        check(pid in offered, "'%s' appears in the profile picker" % pid)


def test_profile_annotations():
    print("TEST profile annotations")

    for p in meta.PROFILES:
        check("description" in p and p["description"],
              "'%s' has a description" % p["id"])
        check(p["firmware"] == (p["id"] in meta.FIRMWARE_PROFILES),
              "'%s' firmware flag is consistent" % p["id"])
        check((p["falls_back_to"] is None) == p["firmware"],
              "'%s' fallback is set exactly when there is no driver" % p["id"])

    check(meta.DEFAULT_PROFILE in meta.FIRMWARE_PROFILES,
          "the default profile is one the master implements")


def test_warnings():
    print("TEST profile warnings")

    cfg = meta.default_config()
    check(meta.profile_warnings(cfg) == [],
          "a default config produces no warnings")

    cfg["slaves"][0]["profile"] = "copley_accelnet"
    warn = meta.profile_warnings(cfg)
    check(len(warn) == 1 and meta.GENERIC_PROFILE in warn[0],
          "a picklist-only family warns about the generic fallback")

    cfg["slaves"][0]["profile"] = "not_a_real_profile"
    warn = meta.profile_warnings(cfg)
    check(len(warn) == 1 and "unknown" in warn[0],
          "an unknown profile id is reported")

    cfg["slaves"][0]["profile"] = ""
    warn = meta.profile_warnings(cfg)
    check(len(warn) == 1 and "no profile set" in warn[0],
          "an empty profile id is reported")

    cfg["slaves"][0]["profile"] = "elmo_platinum"
    cfg["slaves"][0]["expected_vendor_id"] = "0x00000001"
    warn = meta.profile_warnings(cfg)
    check(len(warn) == 1 and "does not match" in warn[0],
          "a vendor id that contradicts the chosen family is flagged")


def test_templates_are_valid():
    print("TEST mode templates")

    for mode, tpl in meta.MODE_TEMPLATES.items():
        rx_idx = [e["index"] for e in tpl["rx"]]
        tx_idx = [e["index"] for e in tpl["tx"]]
        check("0x6040" in rx_idx, "mode %d template maps the controlword" % mode)
        check("0x6041" in tx_idx, "mode %d template maps the statusword" % mode)

    check("0x6064" in [e["index"] for e in meta.MODE_TEMPLATES[8]["tx"]],
          "CSP template maps position actual")
    check("0x606C" in [e["index"] for e in meta.MODE_TEMPLATES[9]["tx"]],
          "CSV template maps velocity actual")
    check("0x6077" in [e["index"] for e in meta.MODE_TEMPLATES[10]["tx"]],
          "CST template maps torque actual")


# --------------------------------------------------------------------------
# Parameter sets
# --------------------------------------------------------------------------
PARAM_SET_H = os.path.join(REPO_ROOT, "src", "params", "param_set.h")
PARAM_SET_C = os.path.join(REPO_ROOT, "src", "params", "param_set.c")


def _param_types_in_c():
    """The type names the C loader accepts, read out of the g_types table."""
    with open(PARAM_SET_C, "r", encoding="utf-8") as fh:
        text = fh.read()
    block = re.search(r"g_types\[PARAM_TYPE__COUNT\]\s*=\s*\{(.*?)\};", text, re.S)
    if not block:
        return set()
    return set(re.findall(r'"([a-z0-9]+)"', block.group(1)))


def test_param_types_match_the_loader():
    print("TEST parameter types match src/params")

    in_c = _param_types_in_c()
    in_gui = {t["id"] for t in param_meta.PARAM_TYPES}

    check(bool(in_c), "found the type table in param_set.c (%d)" % len(in_c))
    check(in_c == in_gui,
          "GUI offers exactly the types the loader accepts (C only: %s, GUI only: %s)"
          % (sorted(in_c - in_gui) or "none", sorted(in_gui - in_c) or "none"))
    check(param_meta.DEFAULT_PARAM_TYPE in in_c,
          "the default type is one the loader knows")

    # Sizes are what decide the SDO length, so a wrong one here writes the
    # wrong number of bytes to a drive.
    expected = {"u8": 1, "i8": 1, "u16": 2, "i16": 2,
                "u32": 4, "i32": 4, "f32": 4}
    for t in param_meta.PARAM_TYPES:
        check(t["bytes"] == expected.get(t["id"]),
              "%s is %d byte(s)" % (t["id"], t["bytes"]))


def test_param_catalogue():
    print("TEST parameter catalogue")

    types = {t["id"] for t in param_meta.PARAM_TYPES}
    seen = set()

    for entry in param_meta.PARAM_CATALOGUE:
        key = (entry["index"], entry["subindex"])
        check(key not in seen, "%s:%02X appears once"
              % (entry["index"], entry["subindex"]))
        seen.add(key)
        check(entry["type"] in types,
              "%s uses a known type (%s)" % (entry["name"], entry["type"]))
        check(entry["group"] in param_meta.PARAM_GROUPS,
              "%s is in a listed group" % entry["name"])
        try:
            int(entry["index"], 16)
            ok = True
        except ValueError:
            ok = False
        check(ok, "%s has a parseable index" % entry["name"])

    # The two signatures below are ASCII and easy to get subtly wrong; a wrong
    # one silently does nothing, which is the worst failure mode there is.
    store = [e for e in param_meta.PARAM_CATALOGUE if e["index"] == "0x1010"]
    check(len(store) == 1 and int(store[0]["value"], 16) == 0x65766173,
          "'store' signature is 'save' (0x65766173)")
    restore = [e for e in param_meta.PARAM_CATALOGUE if e["index"] == "0x1011"]
    check(len(restore) == 1 and int(restore[0]["value"], 16) == 0x64616F6C,
          "'restore' signature is 'load' (0x64616F6C)")


def test_param_warnings():
    print("TEST parameter set warnings")

    empty = param_meta.default_param_set()
    check(any("empty" in w for w in param_meta.param_warnings(empty)),
          "an empty set is flagged")

    tuned = param_meta.default_param_set()
    tuned["parameters"] = [param_meta.default_param_entry()]
    warn = param_meta.param_warnings(tuned)
    check(any("power-cycle" in w for w in warn),
          "tuning without a 'store' entry warns about losing it")

    dup = param_meta.default_param_set()
    dup["parameters"] = [param_meta.default_param_entry(),
                         param_meta.default_param_entry()]
    check(any("more than once" in w for w in param_meta.param_warnings(dup)),
          "a duplicated object is flagged")

    risky = param_meta.default_param_set()
    risky["files"] = [dict(param_meta.default_param_file(), path="fw.bin")]
    check(any("BOOT" in w for w in param_meta.param_warnings(risky)),
          "a BOOT-state file transfer warns about the axis")


def main():
    print("=== config GUI metadata tests ===")

    test_firmware_profiles_match_the_master()
    test_every_firmware_profile_is_offered()
    test_profile_annotations()
    test_warnings()
    test_templates_are_valid()
    test_param_types_match_the_loader()
    test_param_catalogue()
    test_param_warnings()

    print("\n=== %d failed ===" % len(_FAILURES) if _FAILURES else "\n=== all passed ===")
    return 1 if _FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
