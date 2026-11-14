# Observed weaknesses

Found during a quick survey on 2026-11-14. These are **observations with evidence**, not a to-do list.
Verify before acting; some may be harmless.

## Structure
- `controller.cpp` has 4854 lines in a single file: FSM, dosing math, LED, reporting and the
  calendar glue together. `controller.h` alone has 1206 lines of public API.
- `plantOS.yaml` has 110 lambdas. A lot of the business logic lives in YAML (`on_boot`, button handlers,
  intervals, template sensors). That logic is hard to read, hard to test and is compiled into
  `main.cpp`.
- Two `CentralStatusLogger` copies (`plantos_controller/` and the dead `central_status_logger/`).
  `i2c_scanner` includes the controller's copy, a cross-component coupling.
- LED behaviors live as flat `*.cpp` files in the controller. The `led_behaviors/` directory is empty.
- Docs sprawl: ~10k lines in `docs/`, plus `TODOFSMUNIFICATION.md` (2031 lines) and a
  `MIGRATION_GUIDE.md` for a migration that is long finished. Several docs contradict the code.

## Size and speed
- Flash is 86 % full, so there is little headroom for new features or a custom UI.
- 1247 `ESP_LOG*` calls in components. Many format strings stay in flash even when the log level
  filters them out at runtime. The compile-time level is DEBUG.
- 50 uses of `std::string` in the controller (heap churn, code size). `CentralStatusLogger` is ~1.3k lines
  just to build a report.
- MQTT over TLS (mbedTLS) is a large flash and RAM cost. It feeds Grafana, so it is needed.
- Measurements from October (see `docs/WEBUI_PERFORMANCE_CLEANUP_PLAN.md`): loop lag came from LED publishing
  and synchronous HTTP. Both are fixed. No newer loop-time measurement exists.

## Configurability
- The tank volumes, the 120-day schedule, the grow start date and the actuator max durations are all **compile-time**.
  Switching between Anzucht, Mutter and Haupt means editing YAML and reflashing.
- The schedule is one JSON string in YAML with 120 near-identical lines. There is no notion of
  "phases", and no second schedule.
- There is no concept of "which actuators or functions exist in this setup". The FSM assumes the full main-tank hardware
  (air pump during mixing, wastewater pump, 3 nutrient pumps).

## Robustness and tech debt
- 8 `delay()` calls in `ezo_ph_uart.cpp` command paths (known, documented debt).
- The `Taskfile.yml` targets `decode` and `IDFMON` point at `~/.platformio`. Since the
  project-local `PLATFORMIO_CORE_DIR` they won't find the toolchain.
- `secrets.example.yaml` doesn't validate as shipped (empty `wifi_bssid`).
- Stale pin labels (DS18B20 "GPIO23" vs the real GPIO3) in the YAML and the status report, and old pump GPIOs in
  switch comments (see AGENTS.md "Known issues").
- There are no automated tests at all. The pure logic (dosing math, calendar lookup, preset
  parsing) could be tested on the host without hardware. With no flashing possible, that
  might be worth a lot right now.

## Repo hygiene
- `esptool_venv/` (a full Python venv, 452 files) is committed. So are 4 log captures
  and `boot_logs.txt`.
- The `BLE-Shelly` branch (1 commit, Jan 2026, +1353 lines) has gone stale next to `main`.
- `www/webserver-v2.*` is probably unused with `version: 3`.
- `.gitignore` is missing `.venv/` (only excluded locally through `.git/info/exclude`).

## Resolved or added during the sprint
- Resolved 2026-11-14: repo hygiene items above, docs sprawl, the duplicate `CentralStatusLogger`, Taskfile paths,
  `secrets.example.yaml`, stale pin labels.
- **New: EC dose formula is inverted** (`delta_ec / K` with K in mL/L per µS/cm), so EC steps always dose the capped
  calendar amount. See `QUESTIONS.md`.
- **New:** ASG soft-start ramp is never configured. `FSMINFO.md` says PH_MIXING is 120 s, the code scales 30-600 s with
  volume (~244 s at 27.4 L). pH dosing uses the 27.4 L delta volume, not the total.
- **New:** generated `main.cpp.o` is 109 KB (`setup()` 48 KB), larger than the controller.
- **New, from the host tests (each pinned by a `// SUSPECT:` test in `tests/`):**
  - The EC dose is accumulated twice for K learning in the automatic path (`controller.cpp:2336` and `:2370`).
  - The auto-feed "already fed today" marker does not survive a reboot: `AUTOFEED_<date>` is written to the single PSM
    slot and `startFeed()` overwrites it with `FEED_OPERATION`. After a reboot the same day the trigger fires again.
    `AUTORES_<week>` follows the same pattern (by reading, not tested).
  - Reservoir-change weekday and week number are UTC-based; night mode and the feed date are local.
  - `getCurrentGrowDay` flips at 00:00 UTC and has no caller; `__init__.py:110` builds the start timestamp in the build
    machine's timezone.
  - Calendar day numbers above 255 wrap (`"day": 300` → 44). Fallback `ec_tolerance` 0.20 sits in a µS/cm field.
  - `enableCycling("AirPump", false)` leaves the tracked ASG state stale until the next Shelly poll.
  - Not bugs, but easy to break: with the production calendar "night" means **light off** (08:00-16:00, 04:00-16:00 from
    day 22), the 22-9 hour window is only a fallback; `PH_PROCESSING` uses the HAL range 5.5-6.2 while `PH_CALCULATING`
    uses the calendar range; any pH above 5.85 doses at least 0.5 mL; `ph_K_` defaults to 0.25 as a member but 0.07 from
    NVS; the EC guard is 12 h (comments say 4 h).
