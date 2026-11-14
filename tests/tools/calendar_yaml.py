"""Read the `calendar_manager:` block of plantOS.yaml without ESPHome.

The block is cut out as text (from `calendar_manager:` to the next top-level key) and
parsed with PyYAML; it has no ESPHome tags. The rest of plantOS.yaml (`!secret`,
`!lambda`, ...) is never parsed.
"""
import re

import yaml


def load_calendar_block(yaml_path):
    with open(yaml_path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    start = None
    for i, line in enumerate(lines):
        if start is None:
            if re.match(r"^calendar_manager:\s*(#.*)?$", line):
                start = i
            continue
        if re.match(r"^[^\s#]", line):
            return yaml.safe_load("\n".join(lines[start:i]))["calendar_manager"]
    if start is None:
        raise SystemExit(f"calendar_manager: block not found in {yaml_path}")
    return yaml.safe_load("\n".join(lines[start:]))["calendar_manager"]
