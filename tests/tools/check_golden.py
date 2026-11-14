#!/usr/bin/env python3
"""Cross-check golden/haupt_schedule.csv against the Haupt phases in plantOS.yaml.

Independent of the C++ lookup and of phase_tables.py: expands the raw YAML phases day
by day itself (last phase with from_day <= day, EC mS/cm -> uS/cm, "HH:MM" -> minutes)
and requires every one of the 120 CSV rows to match. Guards against a golden file
regenerated from a broken lookup and against an accidental schedule change.

Usage: check_golden.py <plantOS.yaml> <haupt_schedule.csv>
"""
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from calendar_yaml import load_calendar_block  # noqa: E402


def minutes(v):
    if isinstance(v, str):
        h, m = v.split(":")
        return int(h) * 60 + int(m)
    return int(v)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    phases = load_calendar_block(sys.argv[1])["calendars"]["haupt"]["phases"]
    with open(sys.argv[2], encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    errors = []
    if [int(r["day"]) for r in rows] != list(range(1, 121)):
        errors.append("golden days are not exactly 1..120 in order")
    pairs = [
        ("ph_min", "ph_min", float), ("ph_max", "ph_max", float),
        ("dose_A_ml_per_L", "dose_A_ml_per_L", float), ("dose_B_ml_per_L", "dose_B_ml_per_L", float),
        ("dose_C_ml_per_L", "dose_C_ml_per_L", float),
        ("light_on_min", "light_on_time", minutes), ("light_off_min", "light_off_time", minutes),
        ("ec_target_uS_cm", "ec_target", lambda v: float(v) * 1000.0),
        ("ec_tolerance_uS_cm", "ec_tolerance", lambda v: float(v) * 1000.0),
    ]
    for r in rows:
        day = int(r["day"])
        src = None
        for p in phases:
            if p["from_day"] <= day:
                src = p
        if src is None:
            errors.append(f"day {day}: no phase")
            continue
        for col, key, conv in pairs:
            if abs(float(r[col]) - conv(src[key])) > 1e-3:
                errors.append(f"day {day}: {col}={r[col]} but phase from_day {src['from_day']} {key}={src[key]}")
    if errors:
        print("golden/haupt_schedule.csv does not match the haupt calendar in plantOS.yaml:", file=sys.stderr)
        for e in errors[:20]:
            print("  " + e, file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
