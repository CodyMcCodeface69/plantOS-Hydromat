# docs/ index

Last checked against the code: 2026-11-14. When a doc and the code disagree, the code wins.
For the firmware itself start with `AGENTS.md` (architecture, rules) and `FSMINFO.md`
(state machine, authoritative); both are in the repo root.

## Current

| File | Content | Trust |
|---|---|---|
| `CONTROL_LOGIC.md` | Why pH and EC control work the way they do, plus a table of where the code differs from the original specs and one known unit problem in EC dosing | Current (2026-11-14). `FSMINFO.md` wins on behavior. |
| `LESSONS.md` | Rules learned from the RISC-V alignment crash and the watchdog resets | Current |
| `EC_Sensor_Calibration.md` | EC/TDS conversion pipeline, temperature compensation, one-point calibration | Current. Code comments cite its section numbers. |
| `WEBUI_PERFORMANCE_CLEANUP_PLAN.md` | Web UI slowness analysis and the cleanup options | Current (2026-10-04) |
| `Hydroponik_Zielwerttabelle.csv` | Source table for the grow calendar: EC and pH window per growth phase (German) | Reference data. Its EC targets and pH windows match the calendar in `plantOS.yaml`. Excel export in a legacy encoding with damaged special characters and one `#WERT!` row. |

## Interface contracts for other systems

These describe what **other** machines expect from this firmware. Do not change them without
changing those systems. All three depend on the MQTT topic `plantos/vitals` (JSON every 30 s);
its keys match the publish block in `plantOS.yaml`.

| File | Content | Trust |
|---|---|---|
| `DESK_DISPLAY_FIRMWARE.md` | Spec for the e-ink desk display that subscribes to `plantos/vitals` | Contract. Keep the payload keys byte-compatible. |
| `HETZNER_LOGGING_SETUP.md` | Setup plan for the server side: Mosquitto, Telegraf, InfluxDB, Grafana | Contract for the payload. The server steps cannot be checked from this repo. |
| `HETZNER_DEBUG_LOG_CAPTURE.md` | Server-side capture of the device log from `plantos/debug` | Same. What arrives there depends on the firmware's log level. |

## Plans that are not implemented

| File | Content | Trust |
|---|---|---|
| `MOTHERPLANT_CHAMBER_PLAN.md` | Design for a second, pH-only chamber with its own probe and controller | Design only. Check it against the preset work in `plan/03-presets.md` before building on it. |
| `plan-ac-infinity-smart-drying.md` | Humidity-controlled drying with an AC Infinity fan through a Hetzner middleware | Design only, nothing of it exists in the code |

## archive/

Kept unchanged for background. Do not treat as a description of the code.

| File | Content |
|---|---|
| `archive/pH_Regellogik.md` | Original pH control spec (German, March 2026, written for a 70 L tank) |
| `archive/EC_Feeding_Logik.md` | Original EC feeding spec (German) |
| `archive/Steuerungslogik.md` | Original overall control spec: modes, alerts, recovery, vacation mode (German) |
| `archive/ENHANCED_ERROR_HANDLING_TESTS.md` | Manual on-device test cases for the retry and alert logic (January 2026) |

The summary of the three specs, with the differences to the code, is `CONTROL_LOGIC.md`.
Everything else that used to be here (FSM unification plan and migration guide, architecture
diagrams, performance analyses, fix write-ups) was deleted on 2026-11-14 and is in git history
up to commit `5f549e0`.
