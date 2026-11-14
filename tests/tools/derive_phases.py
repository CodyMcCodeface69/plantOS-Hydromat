#!/usr/bin/env python3
"""Derive the Haupt phase table from the old 120-day `schedule_json` (one-off migration aid).

Groups consecutive days with identical values into phases and prints them as YAML flow
mappings for the `calendars: haupt: phases:` list in plantOS.yaml. Phase names come from
docs/Hydroponik_Zielwerttabelle.csv when the phase's days lie inside one row's weeks
(week = ceil(day / 7)); other phases get a neutral name from NEUTRAL_NAMES.

The JSON was removed from plantOS.yaml in the phase-table commit; to re-run against it:

    git show f40a83f:plantOS.yaml > /tmp/old.yaml
    python3 tests/tools/derive_phases.py /tmp/old.yaml docs/Hydroponik_Zielwerttabelle.csv

Usage: derive_phases.py <old plantOS.yaml> <Zielwerttabelle.csv>
"""
import csv
import json
import re
import sys

KEYS = ["ph_min", "ph_max", "dose_A_ml_per_L", "dose_B_ml_per_L", "dose_C_ml_per_L",
        "light_on_time", "light_off_time", "ec_target", "ec_tolerance"]

# Phases whose days span several rows of the CSV.
NEUTRAL_NAMES = {
    43: "Vegetation bis frühe Blüte",
    117: "Flush",
}


def extract_schedule_json(yaml_path):
    """The folded `schedule_json: >-` scalar inside `calendar_manager:` (YAML folding: join by spaces)."""
    lines = open(yaml_path, encoding="utf-8").read().split("\n")
    in_cal = False
    for i, line in enumerate(lines):
        if re.match(r"^calendar_manager:\s*(#.*)?$", line):
            in_cal = True
            continue
        if in_cal and re.match(r"^\S", line) and not line.startswith("#"):
            in_cal = False
        m = re.match(r"^(\s+)schedule_json:\s*(>-|>|\|-|\|)\s*$", line) if in_cal else None
        if not m:
            continue
        ind = len(m.group(1))
        body = []
        for nxt in lines[i + 1:]:
            if nxt.strip() == "":
                continue
            if len(nxt) - len(nxt.lstrip(" ")) <= ind:
                break
            body.append(nxt.strip())
        return json.loads(" ".join(body))
    raise SystemExit(f"schedule_json not found in {yaml_path}")


def csv_week_names(csv_path):
    """[(first_week, last_week, name)] from the target-value table (cp850, ";")."""
    out = []
    with open(csv_path, encoding="cp850", newline="") as f:
        for row in csv.reader(f, delimiter=";"):
            if len(row) < 3:
                continue
            m = re.fullmatch(r"(\d+)(?:\D+(\d+))?", row[2].strip())
            if not m:
                continue
            name = re.sub(r"^[^A-Za-zÄÖÜäöü]+", "", row[0]).strip()
            out.append((int(m.group(1)), int(m.group(2) or m.group(1)), name))
    return out


def group(days):
    days = sorted(days, key=lambda d: d["day"])
    if [d["day"] for d in days] != list(range(1, len(days) + 1)):
        raise SystemExit("days are not 1..N without gaps")
    phases = []
    for d in days:
        vals = tuple(d[k] for k in KEYS)
        if phases and phases[-1]["vals"] == vals:
            phases[-1]["to"] = d["day"]
        else:
            phases.append({"from": d["day"], "to": d["day"], "vals": vals})
    return phases


def name_for(p, weeks):
    w0, w1 = (p["from"] + 6) // 7, (p["to"] + 6) // 7
    for a, b, name in weeks:
        if a <= w0 and w1 <= b:
            return name
    return NEUTRAL_NAMES.get(p["from"], f"Tag {p['from']}-{p['to']}")


def hhmm(minutes):
    return f'"{minutes // 60:02d}:{minutes % 60:02d}"'


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    days = extract_schedule_json(sys.argv[1])
    weeks = csv_week_names(sys.argv[2])
    phases = group(days)
    ind = " " * 8  # list items under calendar_manager: calendars: haupt: phases:
    print(f"{ind}# {len(phases)} phases derived from the {len(days)}-day schedule_json")
    for p in phases:
        v = dict(zip(KEYS, p["vals"]))
        span = f'day {p["from"]}' if p["from"] == p["to"] else f'days {p["from"]}-{p["to"]}'
        print(f'{ind}- from_day: {p["from"]}    # {span}')
        print(f'{ind}  name: "{name_for(p, weeks)}"')
        for k in KEYS:
            val = hhmm(v[k]) if k.startswith("light_") else v[k]
            print(f"{ind}  {k}: {val}")


if __name__ == "__main__":
    main()
