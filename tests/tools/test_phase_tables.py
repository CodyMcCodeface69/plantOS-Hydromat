#!/usr/bin/env python3
"""Tests for components/calendar_manager/phase_tables.py (the build-time validation and
C++ rendering of the calendars). Plain asserts, no pytest needed; run by `make`.

Usage: test_phase_tables.py
"""
import copy
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.join(HERE, "..", "..")
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO, "components", "calendar_manager"))

from calendar_yaml import load_calendar_block  # noqa: E402
import phase_tables as pt  # noqa: E402

TESTS = []


def test(fn):
    TESTS.append(fn)
    return fn


def phase(from_day=1, **kw):
    p = {"from_day": from_day, "ph_min": 5.8, "ph_max": 6.2, "dose_A_ml_per_L": 1.0,
         "dose_B_ml_per_L": 1.0, "dose_C_ml_per_L": 1.0, "light_on_time": "16:00",
         "light_off_time": 480, "ec_target": 1.0, "ec_tolerance": 0.2}
    p.update(kw)
    return p


def good():
    return {k: {"phases": [phase()]} for k in pt.CALENDAR_KEYS}


def rejects(cfg, fragment):
    try:
        pt.validate_calendars(cfg)
    except pt.CalendarError as err:
        assert fragment in str(err), f"expected {fragment!r} in {err}"
        return
    raise AssertionError(f"accepted a bad config (expected error with {fragment!r})")


@test
def production_yaml_validates_and_renders():
    block = load_calendar_block(os.path.join(REPO, "plantOS.yaml"))
    cals = pt.validate_calendars(block["calendars"])
    assert len(cals["haupt"]["phases"]) == 18
    assert cals["haupt"]["days"] == 120
    assert len(cals["mutter"]["phases"]) == 1 and cals["mutter"]["days"] == 0
    text = pt.render_cpp(cals)
    assert "static const CalendarDef CALENDARS[3]" in text
    assert '{"Haupt", PHASES_HAUPT, 18, 120},' in text
    # Idempotent: the normalized output validates to itself (ESPHome may re-validate).
    assert pt.validate_calendars(cals) == cals


@test
def production_haupt_expands_to_the_golden_rows():
    import csv
    block = load_calendar_block(os.path.join(REPO, "plantOS.yaml"))
    cals = pt.validate_calendars(block["calendars"])
    rows = pt.expand(cals["haupt"], 120)
    with open(os.path.join(HERE, "..", "golden", "haupt_schedule.csv"), encoding="utf-8") as f:
        golden = list(csv.DictReader(f))
    assert len(golden) == 120
    for r, g in zip(rows, golden):
        assert r["day"] == int(g["day"])
        assert abs(r["ph_min"] - float(g["ph_min"])) < 1e-6
        assert abs(r["dose_C_ml_per_L"] - float(g["dose_C_ml_per_L"])) < 1e-6
        assert r["light_off_time"] == int(g["light_off_min"])
        assert abs(r["ec_target"] * 1000 - float(g["ec_target_uS_cm"])) < 1e-3
    # Hold: day 121+ = day 120
    assert pt.expand(cals["haupt"], 130)[-1]["ec_target"] == rows[-1]["ec_target"]


@test
def minimal_config_is_accepted_and_normalized():
    cals = pt.validate_calendars(good())
    p = cals["haupt"]["phases"][0]
    assert p["light_on_time"] == 960 and p["light_off_time"] == 480
    assert p["name"] == "" and cals["haupt"]["days"] == 0
    assert isinstance(p["ph_min"], float)


@test
def times_accept_hh_mm_and_minutes():
    assert pt.parse_time("t", "00:00") == 0
    assert pt.parse_time("t", "4:05") == 245
    assert pt.parse_time("t", "23:59") == 1439
    assert pt.parse_time("t", 960) == 960
    for bad in ("24:00", "12:60", "noon", "12", -1, 1440, 1.5, True):
        try:
            pt.parse_time("t", bad)
        except pt.CalendarError:
            continue
        raise AssertionError(f"accepted time {bad!r}")


@test
def all_three_presets_required():
    cfg = good()
    del cfg["mutter"]
    rejects(cfg, "missing preset(s) ['mutter']")
    cfg = good()
    cfg["bloom"] = {"phases": [phase()]}
    rejects(cfg, "unknown preset(s) ['bloom']")
    rejects([phase()], "must be a mapping")


@test
def first_phase_must_start_at_day_1():
    cfg = good()
    cfg["haupt"]["phases"] = [phase(2)]
    rejects(cfg, "first phase must start at day 1")


@test
def phases_sorted_and_unique():
    cfg = good()
    cfg["haupt"]["phases"] = [phase(1), phase(10), phase(5)]
    rejects(cfg, "sorted by from_day")
    cfg["haupt"]["phases"] = [phase(1), phase(10), phase(10)]
    rejects(cfg, "duplicate from_day 10")


@test
def ph_window_must_be_ordered():
    cfg = good()
    cfg["anzucht"]["phases"] = [phase(ph_min=6.2, ph_max=6.2)]
    rejects(cfg, "must be below ph_max")
    cfg["anzucht"]["phases"] = [phase(ph_min=6.5, ph_max=6.0)]
    rejects(cfg, "must be below ph_max")


@test
def ranges_and_types_are_checked():
    cases = [
        (dict(ph_min=2.0), "ph_min: must be within"),
        (dict(ph_max=10.0), "ph_max: must be within"),
        (dict(dose_A_ml_per_L=-0.1), "dose_A_ml_per_L: must be within"),
        (dict(dose_B_ml_per_L=50), "dose_B_ml_per_L: must be within"),
        (dict(ec_target=1200), "ec_target: must be within"),   # uS/cm by mistake
        (dict(ec_tolerance=0.0), "ec_tolerance: must be within"),
        (dict(ec_target="1.2"), "must be a number"),
        (dict(light_on_time="25:00"), "invalid time"),
        (dict(name="x" * 40), "longer than 32 bytes"),
        (dict(name=5), "must be a string"),
        (dict(from_day=0), "from_day: must be within 1..9999"),
        (dict(from_day=1.0), "must be a whole number"),
        (dict(colour="green"), "unknown key(s) ['colour']"),
    ]
    for kw, fragment in cases:
        cfg = good()
        cfg["mutter"]["phases"] = [phase(**kw)]
        rejects(cfg, fragment)
    cfg = good()
    p = phase()
    del p["ec_tolerance"]
    cfg["mutter"]["phases"] = [p]
    rejects(cfg, "missing key(s) ['ec_tolerance']")


@test
def calendar_structure_is_checked():
    cfg = good()
    cfg["haupt"] = {"phases": []}
    rejects(cfg, "non-empty list")
    cfg["haupt"] = {"phases": [phase(1), phase(50)], "days": 30}
    rejects(cfg, "before the last phase starts")
    cfg["haupt"] = {"phases": [phase()], "length": 30}
    rejects(cfg, "unknown key(s) ['length']")


@test
def render_escapes_names_and_keeps_order():
    cfg = good()
    cfg["haupt"]["phases"] = [phase(name='Blüte "A"\\'), phase(8, ph_min=6.0, ph_max=6.4)]
    cfg["haupt"]["days"] = 120
    text = pt.render_cpp(pt.validate_calendars(cfg))
    assert r'"Bl\303\274te \"A\"\\"' in text
    assert "  {8, 6.0f, 6.4f," in text
    assert text.index("PHASES_ANZUCHT[]") < text.index("PHASES_MUTTER[]") < text.index("PHASES_HAUPT[]")


@test
def validation_does_not_modify_its_input():
    cfg = good()
    before = copy.deepcopy(cfg)
    pt.validate_calendars(cfg)
    assert cfg == before


def main():
    failed = 0
    for fn in TESTS:
        try:
            fn()
        except AssertionError as err:
            failed += 1
            print(f"FAIL phase_tables.{fn.__name__}: {err}", file=sys.stderr)
    print(f"{len(TESTS)} python tests, {failed} failed")
    if failed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
