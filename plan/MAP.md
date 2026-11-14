# Code map (as of `99a181e`, 2026-11-14)

Line numbers are approximate. Grep before you trust them.

## Top level

| Path | What | Note |
|---|---|---|
| `plantOS.yaml` | **The firmware config**, 2740 lines, 110 lambdas | The section index is in `AGENTS.md` |
| `packages/debug.yaml` | Debug-only entities (~700 lines) | The include is commented out in `plantOS.yaml` (~line 9) |
| `components/` | Custom C++ components, ~18k lines | See below |
| `Taskfile.yml` | build/flash/logs/decode | `decode` and `IDFMON` use `$HOME/.platformio/...`, but the core dir is `./.platformio` |
| `FSMINFO.md` | **Authoritative** FSM description (779 lines) | |
| `AGENTS.md` (`CLAUDE.md` → symlink) | Agent guide: architecture, rules, pins | |
| `PINOUT.md`, `PINOUT_CHANGE.md` | Pin table (current) and migration notes (history) | |
| `CHANGELOG.md` | 1914 lines | |
| `TODO.md` | Stale (Dec 2025) and maintained by hand by the owner | Mentions the old user path `/home/cody/...` |
| `docs/` | 30 topic docs, ~10k lines | Most of them are stale. Trust levels are in `AGENTS.md` |
| `www/webserver-v2.*` | An old v2 web frontend | Probably unused (`web_server: version 3, local: true`) |
| `shelly-scripts/airpump-sequence.js` | A script that runs **on the Shelly** | |
| `Hardware/` | PCB, Gerber, BOM, Altium | Keep |
| `flake.nix/.lock` | Nix devshell (ESPHome 2025.4.2) | Not used on this laptop. The owner uses it on other machines, so keep it |
| `secrets.example.yaml` | Template | `wifi_bssid: ""` fails validation on 2025.4.2. Needs a MAC or the key removed |
| ballast | `esptool_venv/` (452 files in git), `plantos-log-*.txt` ×4, `boot_logs.txt`, `plantos.pdf`, `wifi_scan.h`, `ezo_switch_helper.h` | Check whether the two headers are referenced |

## Components (lines = .cpp + .h)

| Component | ~Lines | Role | Used |
|---|---|---|---|
| `plantos_controller` | 8.3k | FSM (22 states), business logic, LED behaviors, `CentralStatusLogger` | yes |
| ↳ `controller.cpp` | 4854 | Dispatch at ~298-386, `handleXxx()` | |
| ↳ `controller.h` | 1206 | States at 47-70, public API at 200-440 | |
| `plantos_hal` | 2.0k | The only hardware access point. Shelly HTTP worker task, tank volumes, pump flow rates | yes |
| `actuator_safety_gate` | 1.5k | Debounce, max durations, PWM ramp, runtime tracking | yes |
| `ezo_ph_uart` | 1.2k | EZO pH over UART (still has `delay()`) | yes |
| `calendar_manager` | 0.7k | Day schedule from the JSON in YAML, day in NVS | yes |
| `persistent_state_manager`, `wdt_manager`, `alert_service`, `i2c_scanner`, `sensor_filter`, `tds_sensor` | small | Support and sensors | yes |
| `central_status_logger`, `ezo_ph`, `time_dummy`, `psm_checker`, `dummy_actuator_trigger`, `sensor_dummy` | | Dead (not referenced; `sensor_dummy` is only in `external_components`) | **no** |

Python codegen per component: `components/<name>/__init__.py` (schema + `to_code`). Config keys
reach C++ setters only through it. That is where any new config surface ends up.

## Where configuration lives today (relevant for presets)

| Value | Where | Changeable at runtime? |
|---|---|---|
| Tank volume delta / total (27.4 / 47.5 L) | `plantos_hal:` block → `getTankVolumeDelta()` / `getTotalTankVolume()` | no |
| Pump flow rates, PWM | `plantos_hal:` block. PWM is also a `number` | PWM yes |
| 120-day schedule (pH range, A/B/C mL/L, light times, EC target) | `calendar_manager: schedule_json` (YAML string) | no |
| Grow start date `2025-12-30` | `plantos_controller: grow_start_date` | day offset via buttons / NVS |
| pH thresholds, interval | `plantos_hal:` + calendar | partly |
| Night mode, K-factor, EC multiplier, reservoir day, air pump cycle, mother mix volume | 13 `number` entities (~line 2374+) | yes |
| Max actuator durations, PWM sync | `on_boot` lambda (~line 19) | no |
| Shelly IP 192.168.0.130 | HAL | no |

The existing mother support is minimal: the button "Mother Plant Nutrients" and the number "Mother Mix Volume"
→ `startMotherNutrients(vol)`. A larger design doc exists (`docs/MOTHERPLANT_CHAMBER_PLAN.md`,
second chamber with its own pH probe), but it is **not implemented**.

## Build baseline (2026-11-14, ESPHome 2025.4.2, production build without the debug package)

- Flash **86.3 %** (1,583,438 / 1,835,008 B app partition). RAM 12.0 % (39 KB static).
- **No `flash_size` is set**, so the default 4 MB layout applies. The board (Waveshare ESP32-C6-DEV-KIT-**N8**) has
  **8 MB**. A bigger app partition needs a USB flash (the partition table changes), so it can only be prepared now.
- The `BLE-Shelly` branch (Jan 2026) already carries a `huge_app.csv` partition attempt.
- Web UI: ~82 entities in production, 10 sorting groups, embedded 2025.4 v3 frontend.

## Device facts

- `plantos` @ http://192.168.0.201. Shelly Plus 4PM @ 192.168.0.130 (sockets 0 air, 2 wastewater, 3 light).
- MQTT/TLS to the Hetzner broker, topic `plantos/vitals` every 30 s. The desk display consumes it
  (`docs/DESK_DISPLAY_FIRMWARE.md` is the contract, don't break it).
