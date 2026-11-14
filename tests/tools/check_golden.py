#!/usr/bin/env python3
"""Cross-check golden/haupt_schedule.csv against the schedule JSON, independent of the C++ parser.

Guards against a golden file that was regenerated from a broken parser: every CSV row must
equal the JSON row (EC converted mS/cm -> uS/cm) and days must be exactly 1..120.

Usage: check_golden.py <schedule.json> <haupt_schedule.csv>
"""
import csv
import json
import sys


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    with open(sys.argv[1], encoding="utf-8") as f:
        days = {d["day"]: d for d in json.load(f)}
    with open(sys.argv[2], encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    errors = []
    if [int(r["day"]) for r in rows] != list(range(1, 121)):
        errors.append("golden days are not exactly 1..120 in order")
    pairs = [
        ("ph_min", "ph_min", 1.0), ("ph_max", "ph_max", 1.0),
        ("dose_A_ml_per_L", "dose_A_ml_per_L", 1.0), ("dose_B_ml_per_L", "dose_B_ml_per_L", 1.0),
        ("dose_C_ml_per_L", "dose_C_ml_per_L", 1.0),
        ("light_on_min", "light_on_time", 1.0), ("light_off_min", "light_off_time", 1.0),
        ("ec_target_uS_cm", "ec_target", 1000.0), ("ec_tolerance_uS_cm", "ec_tolerance", 1000.0),
    ]
    for r in rows:
        src = days.get(int(r["day"]))
        if src is None:
            errors.append(f"day {r['day']}: not in JSON")
            continue
        for col, key, factor in pairs:
            if abs(float(r[col]) - src[key] * factor) > 1e-3:
                errors.append(f"day {r['day']}: {col}={r[col]} but JSON {key}={src[key]}")
    if errors:
        print("golden/haupt_schedule.csv does not match plantOS.yaml:", file=sys.stderr)
        for e in errors[:20]:
            print("  " + e, file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
