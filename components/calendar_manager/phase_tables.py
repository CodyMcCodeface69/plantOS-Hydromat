"""Calendar phase tables: validation and C++ rendering.

Plain Python on purpose (no ESPHome imports), so the host tests run exactly the code
the firmware build runs: `__init__.py` calls `validate_calendars()` from the config
schema and emits `render_cpp()` into main.cpp; `tests/tools/gen_calendar_tables.py`
calls the same two functions to build the header the host tests compile against.

Format (one entry per preset, all three required):

    calendars:
      haupt:
        days: 120                 # optional: planned length (UI "day 69/120"); omit = open-ended
        phases:
          - from_day: 1           # phase applies from this day until the next phase starts
            name: "Seedling"      # optional label (max 32 chars)
            ph_min: 5.8
            ph_max: 6.2
            dose_A_ml_per_L: 0.8  # mL per liter of tank volume
            dose_B_ml_per_L: 0.8
            dose_C_ml_per_L: 0.8
            light_on_time: "16:00"   # "HH:MM" or minutes since midnight (0..1439)
            light_off_time: "08:00"
            ec_target: 0.50          # mS/cm, 0 = EC feeding off
            ec_tolerance: 0.10       # mS/cm

After the last phase its values apply forever ("hold the last phase").
"""

import re

# Index order is part of the C++ API (CalendarManager::set_active_calendar).
CALENDAR_KEYS = ("anzucht", "mutter", "haupt")
CALENDAR_LABELS = {"anzucht": "Anzucht", "mutter": "Mutter", "haupt": "Haupt"}

# Largest grow day the firmware counts to (uint16_t, ~27 years). Keep in sync with
# CalendarManager.h MAX_DAY.
MAX_DAY = 9999
MAX_NAME_LEN = 32

# key -> (min, max) inclusive; all phase fields except `name` are required.
_FLOAT_FIELDS = {
    "ph_min": (3.0, 9.0),
    "ph_max": (3.0, 9.0),
    "dose_A_ml_per_L": (0.0, 10.0),
    "dose_B_ml_per_L": (0.0, 10.0),
    "dose_C_ml_per_L": (0.0, 10.0),
    "ec_target": (0.0, 5.0),
    "ec_tolerance": (0.01, 2.0),
}
_TIME_FIELDS = ("light_on_time", "light_off_time")
_PHASE_KEYS = {"from_day", "name", *_FLOAT_FIELDS, *_TIME_FIELDS}
_CALENDAR_KEYS = {"days", "phases"}


class CalendarError(ValueError):
    """Invalid calendar configuration. `path` names the offending entry."""

    def __init__(self, path, msg):
        super().__init__(f"{path}: {msg}")
        self.path = path


def _is_number(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def _int(path, v, lo, hi):
    if isinstance(v, bool) or not isinstance(v, int):
        raise CalendarError(path, f"must be a whole number, got {v!r}")
    if not lo <= v <= hi:
        raise CalendarError(path, f"must be within {lo}..{hi}, got {v}")
    return int(v)


def parse_time(path, v):
    """Minutes since midnight from an int (0..1439) or an "HH:MM" string."""
    if isinstance(v, str):
        m = re.fullmatch(r"\s*(\d{1,2}):(\d{2})\s*", v)
        if not m:
            raise CalendarError(path, f'must be "HH:MM" or minutes since midnight, got {v!r}')
        h, mi = int(m.group(1)), int(m.group(2))
        if h > 23 or mi > 59:
            raise CalendarError(path, f"invalid time {v!r}")
        return h * 60 + mi
    return _int(path, v, 0, 1439)


def _phase(path, raw):
    if not isinstance(raw, dict):
        raise CalendarError(path, "a phase must be a mapping")
    unknown = sorted(set(raw) - _PHASE_KEYS)
    if unknown:
        raise CalendarError(path, f"unknown key(s) {unknown}")
    missing = sorted((_PHASE_KEYS - {"name"}) - set(raw))
    if missing:
        raise CalendarError(path, f"missing key(s) {missing}")

    out = {"from_day": _int(f"{path}.from_day", raw["from_day"], 1, MAX_DAY)}
    name = raw.get("name", "")
    if not isinstance(name, str):
        raise CalendarError(f"{path}.name", "must be a string")
    if len(name.encode("utf-8")) > MAX_NAME_LEN:
        raise CalendarError(f"{path}.name", f"longer than {MAX_NAME_LEN} bytes")
    out["name"] = str(name)
    for key, (lo, hi) in _FLOAT_FIELDS.items():
        v = raw[key]
        if not _is_number(v):
            raise CalendarError(f"{path}.{key}", f"must be a number, got {v!r}")
        if not lo <= v <= hi:
            raise CalendarError(f"{path}.{key}", f"must be within {lo}..{hi}, got {v}")
        out[key] = float(v)
    for key in _TIME_FIELDS:
        out[key] = parse_time(f"{path}.{key}", raw[key])
    if out["ph_min"] >= out["ph_max"]:
        raise CalendarError(path, f"ph_min {out['ph_min']} must be below ph_max {out['ph_max']}")
    return out


def _calendar(path, raw):
    if not isinstance(raw, dict):
        raise CalendarError(path, "must be a mapping with `phases` (and optional `days`)")
    unknown = sorted(set(raw) - _CALENDAR_KEYS)
    if unknown:
        raise CalendarError(path, f"unknown key(s) {unknown}")
    phases_raw = raw.get("phases")
    if not isinstance(phases_raw, list) or not phases_raw:
        raise CalendarError(f"{path}.phases", "must be a non-empty list")
    if len(phases_raw) > 255:
        raise CalendarError(f"{path}.phases", "at most 255 phases")
    phases = [_phase(f"{path}.phases[{i}]", p) for i, p in enumerate(phases_raw)]
    if phases[0]["from_day"] != 1:
        raise CalendarError(f"{path}.phases[0].from_day", "the first phase must start at day 1")
    for i in range(1, len(phases)):
        prev, cur = phases[i - 1]["from_day"], phases[i]["from_day"]
        if cur == prev:
            raise CalendarError(f"{path}.phases[{i}].from_day", f"duplicate from_day {cur}")
        if cur < prev:
            raise CalendarError(f"{path}.phases[{i}].from_day",
                                f"phases must be sorted by from_day ({cur} after {prev})")
    days = raw.get("days", 0)
    if days != 0:
        days = _int(f"{path}.days", days, 1, MAX_DAY)
        if days < phases[-1]["from_day"]:
            raise CalendarError(f"{path}.days",
                                f"{days} is before the last phase starts (day {phases[-1]['from_day']})")
    return {"days": days, "phases": phases}


def validate_calendars(raw):
    """Validate the `calendars:` mapping. Returns a normalized copy (times in minutes,
    floats, `name` and `days` filled in). Raises CalendarError."""
    if not isinstance(raw, dict):
        raise CalendarError("calendars", "must be a mapping of preset -> calendar")
    unknown = sorted(set(raw) - set(CALENDAR_KEYS))
    if unknown:
        raise CalendarError("calendars", f"unknown preset(s) {unknown}, expected {list(CALENDAR_KEYS)}")
    missing = [k for k in CALENDAR_KEYS if k not in raw]
    if missing:
        raise CalendarError("calendars", f"missing preset(s) {missing}")
    return {k: _calendar(f"calendars.{k}", raw[k]) for k in CALENDAR_KEYS}


def expand(calendar, last_day):
    """Day-by-day rows 1..last_day (hold the last phase), for checks and tests."""
    phases = calendar["phases"]
    rows = []
    for day in range(1, last_day + 1):
        cur = phases[0]
        for p in phases:
            if p["from_day"] <= day:
                cur = p
        rows.append(dict(cur, day=day))
    return rows


def _f(v):
    r = repr(float(v))
    return r + "f"


def _cstr(s):
    out = []
    for b in s.encode("utf-8"):
        c = chr(b)
        if c in '"\\':
            out.append("\\" + c)
        elif 32 <= b < 127:
            out.append(c)
        else:
            out.append(f'\\{b:03o}')  # octal escape: no greedy hex-digit merge
    return '"' + "".join(out) + '"'


# Fully qualified name of the generated table, passed to set_calendars().
TABLE_SYMBOL = "esphome::calendar_manager::generated::CALENDARS"


def render_cpp(calendars):
    """C++ definitions of the phase tables for validated `calendars`.

    Needs the types from calendar_manager/phase_table.h. All data is `static const`
    with constant initializers, so it lands in .rodata (flash), not in RAM.
    """
    lines = ["namespace esphome {", "namespace calendar_manager {", "namespace generated {"]
    for key in CALENDAR_KEYS:
        lines.append(f"static const Phase PHASES_{key.upper()}[] = {{")
        for p in calendars[key]["phases"]:
            lines.append(
                "  {%d, %s, %s, %s, %s, %s, %d, %d, %s, %s, %s}," % (
                    p["from_day"], _f(p["ph_min"]), _f(p["ph_max"]),
                    _f(p["dose_A_ml_per_L"]), _f(p["dose_B_ml_per_L"]), _f(p["dose_C_ml_per_L"]),
                    p["light_on_time"], p["light_off_time"],
                    _f(p["ec_target"]), _f(p["ec_tolerance"]), _cstr(p["name"])))
        lines.append("};")
    lines.append(f"static const CalendarDef CALENDARS[{len(CALENDAR_KEYS)}] = {{")
    for key in CALENDAR_KEYS:
        cal = calendars[key]
        lines.append("  {%s, PHASES_%s, %d, %d}," % (
            _cstr(CALENDAR_LABELS[key]), key.upper(), len(cal["phases"]), cal["days"]))
    lines.append("};")
    lines += ["}  // namespace generated", "}  // namespace calendar_manager", "}  // namespace esphome"]
    return "\n".join(lines) + "\n"
