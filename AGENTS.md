# CLAUDE.md / AGENTS.md

This file gives coding agents guidance for working in this repository.
`CLAUDE.md` is a symlink to `AGENTS.md`.
It was rewritten on 2026-10-04 against commit `8830ccf`. When this file disagrees with the
code, **the code wins**. Fix this file when you notice drift.

> **Overhaul sprint until 2026-11-16 23:59:** start with `plan/README.md`, then `plan/LOG.md`.
> Build only, no flashing (the device is out of reach).

## Project Overview

PlantOS ("Hydromat Mk.1") is an ESPHome firmware for an ESP32-C6 that runs a hydroponic
tank. It covers pH correction, EC-based nutrient feeding, water fill and empty, reservoir
change, night mode and grow-light scheduling over a 120-day grow calendar. The system is
**deployed and running in production**: on 2026-10-04 it had 168 h uptime, grow day 69/120.

- **Toolchain**: ESPHome **2025.4.2** (from the Nix flake), framework ESP-IDF, single-core RISC-V
- **Board**: Waveshare ESP32-C6-DEV-KIT-N8. The YAML uses `esp32-c6-devkitc-1`.
- **Size**: `plantOS.yaml` has ~2700 lines (+ ~700 in `packages/debug.yaml`); custom C++ has ~18k lines in `components/`
- **Branch**: `main`
- **Device**: `plantos` at http://192.168.0.201 (web_server v3, port 80)

### Documents: what to trust

| Doc | Trust |
|---|---|
| `FSMINFO.md` | **Authoritative for FSM behavior** (see below) |
| `PINOUT.md` | Current, verified against `plantOS.yaml` on 2026-11-14 |
| `docs/README.md` | Index of `docs/` with a trust level per file. Start there for topic docs. |
| `docs/CONTROL_LOGIC.md` | Current. Rationale for pH/EC control, spec-vs-code table, the known EC dosing unit problem. |
| `docs/LESSONS.md` | Current. RISC-V alignment, no logging from callbacks/tasks, watchdog. |
| `docs/WEBUI_PERFORMANCE_CLEANUP_PLAN.md` | Current. Web UI slowness analysis and options. |
| `docs/DESK_DISPLAY_FIRMWARE.md`, `docs/HETZNER_*.md` | Interface contracts (`plantos/vitals`, `plantos/debug`). Do not break. |
| `docs/archive/*` | Background only, not a description of the code |
| `TODO.md` | The owner's file, reorganized 2026-11-14. The user edits it by hand. |
| `CHANGELOG.md` | Historical, ends at 0.9.1 (2025-12-15) |

## Commands (Taskfile)

```bash
task build             # compile (minutes cold, ~30 s incremental)
task test              # host unit tests (~1 s incremental)
task flash             # flash via USB
task run               # build + flash + logs   (OTA=true for OTA, IDFMON=true for decoded backtraces)
task snoop             # attach to logs only (USB, OTA fallback)
task log DURATION=1800 # capture logs to a timestamped file
task config_validate   # validate plantOS.yaml
task decode -- 0x...   # addr2line for crash addresses
task reboot | clean
```

Enter the environment with `direnv allow` or `nix develop`. Without Nix, use a venv with
`esphome==2025.4.2` (on the owner's Ubuntu laptop: `source .venv/bin/activate`; `task` is in
`~/.local/bin`). Host-side unit tests for the pure logic live in `tests/` (`task test`).
Verify everything else on hardware through the logs, the web UI and the LED.

## Architecture: 3-layer HAL (mandatory)

```
Layer 1  plantos_controller   FSM, all business logic, LED behaviors, CentralStatusLogger
           │  actuator commands
Layer 2  actuator_safety_gate debounce, max durations, soft-start/stop (PWM ramp), cycling, runtime tracking
           │
Layer 3  plantos_hal          ONLY place touching hardware: GPIO/PWM outputs, sensors, LED,
                              Shelly Plus 4PM over HTTP (192.168.0.130)
```

- **Do not** create standalone components or YAML lambdas that drive actuators directly.
  New hardware gets a HAL method first: an abstract virtual in `HAL`, implemented in
  `ESPHomeHAL`, plus a setter/getter/callback triplet. It is wired through ASG and then
  called by the Controller.
- **HAL DI**: add a `CONF_*` key, add it to `CONFIG_SCHEMA`, then call `get_variable` and
  `cg.add(setter)` in `to_code()` (`components/plantos_hal/__init__.py`).
- Services injected into the controller: PSM (`persistent_state_manager`), `calendar_manager`,
  `alert_service`, the SNTP time source, and the HAL and ASG.

### Actuators (ASG / HAL IDs)

| ID | Hardware | Path |
|---|---|---|
| `WaterValve` | GPIO1 (MagValve) | LEDC PWM |
| `AcidPump` | GPIO4 (PP_1) | LEDC PWM |
| `NutrientPumpA` (Grow) | GPIO5 (PP_2) | LEDC PWM |
| `NutrientPumpB` (Micro) | GPIO6 (PP_3) | LEDC PWM |
| `NutrientPumpC` (Bloom) | GPIO7 (PP_4) | LEDC PWM |
| `AirPump` | Shelly socket 0 | HTTP |
| `WastewaterPump` | Shelly socket 2 | HTTP |
| `GrowLight` | Shelly socket 3 | HTTP |

The `on_boot` lambda at the top of `plantOS.yaml` sets the max durations and the PWM sync.
Pump flow rates (mL/s) are configured in the `plantos_hal:` block.

### Sensors and pins

| GPIO | Function |
|---|---|
| 0 | TDS/EC analog (KS0429) through the `tds_sensor` component. Raw ADC comes from the leon-v ESPHome fork, because the stock ADC fails to compile on the C6. |
| 3 | DS18B20 water temperature (1-Wire) |
| 8 | WS2812 system LED (RMT) |
| 17 / 23 / 22 | Water level HIGH / LOW / EMPTY (XKC-Y23-V) |
| 18 / 19 | EZO pH UART TX / RX, **9600 baud** (`ezo_ph_uart`) |
| 20 / 21 | I2C SDA / SCL, BME280 at 0x76 (air temp, humidity, pressure) |

pH path: `ezo_ph_uart` → `sensor_filter` (outlier rejection) → HAL `readPH()`.

## FSM (CRITICAL)

**`FSMINFO.md` is the authoritative source for controller FSM behavior.**
Consult it before you touch states, transitions, triggers, actuator actions, timeouts or
PSM recovery. **Update it** in the same change whenever you modify any of those things.

- 22 states: `components/plantos_controller/controller.h:47-70`.
  The groups are INIT/IDLE/NIGHT/SHUTDOWN/PAUSE/ERROR, PH_*, EC_*, FEEDING, WATER_FILLING/EMPTYING
  and FEED_FILLING.
- Dispatch switch in `controller.cpp` `loop()`; the `handleXxx()` handlers live in the role files (grep for them).
- Public API (start*/set*/is*, auto toggles, setToShutdown/Pause/Idle, emergencyStop):
  `controller.h:200-440`.
- pH dosing uses a K-factor (EMA-updated, exposed as the `ph_k_factor_slider` number).
  EC feeding uses `ec_K_feed_`.

## Component inventory (`components/`)

Components used in `plantOS.yaml`:

| Component | Notes |
|---|---|
| `plantos_controller` (~8.3k lines) | Split by role: `controller.cpp` (setup/loop/dispatch/API), `ph_control.cpp`, `ec_control.cpp`, `water_ops.cpp`, `scheduling.cpp`, `health_checks.cpp`; pure math in `dosing_math.*` and `schedule_math.*` (no ESPHome includes, host-tested). LED behaviors are in flat `*.cpp` files (`breathing_green.cpp` etc.). `CentralStatusLogger.*` builds the on-demand status report (`i2c_scanner` reports hardware status into it). |
| `plantos_hal` (~2.5k) | Shelly HTTP runs in a FreeRTOS worker task (`shellyTaskLoop`, `esp_http_client`, 2 s timeout, poll every 30 s). Main thread ↔ task only via two queues; results are applied in `ESPHomeHAL::loop()`. The runtime switch "Shelly Integration" (`setShellyEnabled`) turns it off. |
| `actuator_safety_gate` (~1.5k) | |
| `ezo_ph_uart`, `sensor_filter`, `tds_sensor` | sensors |
| `calendar_manager` | 120-day schedule JSON in YAML (doses in mL/L, pH range, EC target). Day is kept in NVS. |
| `persistent_state_manager`, `wdt_manager`, `i2c_scanner`, `alert_service` | support services |

`includes/wifi_scan.h` is pulled in through `esphome: includes:` for the "WiFi Scan" button lambda.

## plantOS.yaml layout (approximate line numbers)

```
9 packages (commented out) · 19 esphome/on_boot · 75 logger · 109 wifi · 194 web_server (10 sorting_groups)
242 api · 258 mqtt (TLS, Hetzner; publishes plantos/vitals every 30 s) · 275 http_request
302 external_components · 328 uart · 356 light (internal) · 391 output · 447 sensor · 610 binary_sensor
694 text_sensor · 811 button · 1537 plantos_hal · 1618 plantos_controller · 1664 ASG · 1696 ezo_ph_uart
1795 calendar_manager · 1936 switch · 2374 number (13) · 2659 interval · 2698 script
```

**Debug package:** `packages/debug.yaml` holds the debug-only entities (verbose log switches,
direct GPIO switches, HAL mirror text sensors, Shelly and GPIO test buttons). Production builds
leave it out; uncomment the two `packages:` lines in `plantOS.yaml` for a debug build. Never put
anything that core lambdas reference by `id()` into it.

There are about 82 web UI entities in the production build (~112 with the debug package). Each one carries `web_server: sorting_group_id`
(`sg_status`, `sg_sensors`, `sg_controller_actions`, `sg_controller_settings`, `sg_calendar`,
`sg_actuators`, `sg_system`, `sg_debug_logging`, `sg_debug_gpio`, `sg_debug_hal`).
Give every new entity a group.

## Known issues

- **Web UI frontend must match the firmware.** `web_server: local: true` embeds the 2025.4
  frontend. The remote `oi.esphome.io/v3/www.js` is always the newest and drops buttons,
  switches and numbers on 2025.4 (no `domain` field in SSE events). Re-check after an ESPHome
  upgrade. Background: `docs/WEBUI_PERFORMANCE_CLEANUP_PLAN.md`.
- PlatformIO lives in `<repo>/.platformio` (`PLATFORMIO_CORE_DIR`, set in `Taskfile.yml` and the
  devshell), not the shared `~/.platformio`. Other ESPHome projects on other versions swapped the
  toolchain there. After changing it, run `task clean` once.
- **New `.cpp` files are not picked up by an incremental build** (the ESP-IDF CMake file list is cached; the link then
  fails with undefined references). After adding a source file run
  `touch .esphome/build/plantos/src/CMakeLists.txt .esphome/build/plantos/CMakeLists.txt` (or `task clean`) and build again.
- **Flash size:** the device runs the 4 MB layout. `plantOS.yaml` has a commented opt-in for 8 MB (`partitions_8mb.csv`,
  keeps NVS in place). It needs **one USB flash**; never OTA an 8 MB build onto the 4 MB layout, and never set
  `flash_size` alone (ESPHome's generated layout moves NVS and loses grow day, K-factors and calibration).
- **EC dosing math is inverted** (`controller.cpp`, `handleEcCalculating`): `ec_K_feed_` is mL/L per µS/cm, but the dose is
  `delta_ec / ec_K_feed_`. The result is always cut down to the calendar dose, so every EC step doses the full calendar
  amount and the learned K has no effect. Not fixed without the owner (see `plan/QUESTIONS.md`).
- **Soft-start is not active**: `ActuatorSafetyGate::setRampDuration()` is never called, so outputs switch at once.

## Coding rules

- **Non-blocking only**: no `delay()` in `loop()` paths. Use `millis()` deltas. The existing
  `delay()` calls in `ezo_ph_uart.cpp` command paths are tech debt; don't add more.
  Network I/O goes into the HAL's Shelly worker pattern, never into `loop()`.
- No `ESP_LOG*` from ISR, callback or worker-task context. Set a flag and log in `loop()`
  (RISC-V alignment crashes).
- Template switch lambdas must return cached values. Don't call NVS `loadState()` in them.
- ESPHome sensor component layout: `__init__.py` (empty), `sensor.py` (schema), `*.h/*.cpp`.
  Use `PollingComponent` for periodic work and `Component` for event-driven work.
- Name new components for their eventual role, not for their first feature.
- **Git:** agents may commit and push on their own. Use small commits that build, with short English
  messages. Keep only `main`: no branches are left for the user to merge, and any short-lived
  branch gets merged and deleted by the agent. Never commit `secrets.yaml`.
