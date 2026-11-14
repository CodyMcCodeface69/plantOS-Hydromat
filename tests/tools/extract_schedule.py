#!/usr/bin/env python3
"""Extract the calendar `schedule_json` block from plantOS.yaml.

Plain text extraction (no YAML library, no `!secret` handling needed): finds the
`schedule_json: >-` key inside the `calendar_manager:` block and joins the folded
scalar the way YAML does (lines joined by a single space, trailing newline stripped).

Usage: extract_schedule.py <plantOS.yaml> <out.json>
"""
import json
import re
import sys


def extract(yaml_path):
    with open(yaml_path, encoding="utf-8") as f:
        lines = f.read().split("\n")

    in_calendar = False
    for i, line in enumerate(lines):
        if re.match(r"^calendar_manager:\s*(#.*)?$", line):
            in_calendar = True
            continue
        if in_calendar and re.match(r"^\S", line) and not line.startswith("#"):
            in_calendar = False
        m = re.match(r"^(\s+)schedule_json:\s*(>-|>|\|-|\|)\s*$", line) if in_calendar else None
        if not m:
            continue
        key_indent = len(m.group(1))
        body = []
        for nxt in lines[i + 1:]:
            if nxt.strip() == "":
                continue
            indent = len(nxt) - len(nxt.lstrip(" "))
            if indent <= key_indent:
                break
            body.append(nxt.strip())
        return " ".join(body)
    raise SystemExit(f"schedule_json block not found in {yaml_path}")


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    text = extract(sys.argv[1])
    # Fail early with a clear message if the block is not valid JSON.
    data = json.loads(text)
    if not isinstance(data, list):
        raise SystemExit("schedule_json is not a JSON array")
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write(text)


if __name__ == "__main__":
    main()
