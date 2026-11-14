# Implementation plan

Written 2026-11-14 against `1655e15`. This is the ordered work list for the sprint. The workstream
files (`01`–`04`) hold intent and constraints; this file holds **what gets done in which order** and
the decisions already taken. Tick steps off here and write the numbers and details into `LOG.md`.

## Decisions from the owner (2026-11-14)

| Topic | Decision |
|---|---|
| Grow day | **Per preset.** Each preset has its own start date / day counter in NVS. Switching away and back resumes by date. |
| Calendar end | **Hold the last phase.** Calendars are phase tables (day ranges). After the last phase its values apply forever. |
| Editing | Tank volumes and function switches are **UI entities** acting on the active preset (NVS, YAML gives defaults). Calendars stay in **YAML**. |
| Refactoring | **Aggressive is allowed**, but every change that might alter behavior goes into `plan/TEST_ON_DEVICE.md` with a concrete check. |
| Mutter hardware | Nutrient pumps A/B/C, water valve + level sensors, wastewater pump. **No air pump, no grow light.** |
| Anzucht hardware | Not answered. Default: pH probe + acid pump only (parked in `QUESTIONS.md`). |
| Log level | Production compiles at **INFO**, the debug package raises it to DEBUG. |
| `BLE-Shelly` | Tag `archive/BLE-Shelly`, delete the branch, carry over only the 8 MB partition idea as an opt-in. |

## Rules for every step

- One step = one or a few small commits on `main`. Each passes `task config_validate` and `task build`. Push after each.
- Record flash / RAM from the build output in `LOG.md` whenever a step is meant to change them.
- A change to states, transitions, triggers, actuator actions or timeouts updates `FSMINFO.md` in the same commit.
- Anything that may behave differently on hardware gets a row in `TEST_ON_DEVICE.md`: what changed,
  commit, how to check it, what a failure looks like.
- Disk is at 98 %. Run `task clean` before a full rebuild only when needed, and check `df -h /` before big builds.
- A debug-package build (`packages:` lines uncommented) must still compile after phases 1, 2 and 3. Don't commit it uncommented.

## Phase 0: safety net (before touching logic)

- [x] 0.1 Baseline: build `1655e15`, record flash/RAM and the largest symbols
      (`riscv32-esp-elf-nm --size-sort` on the ELF, top 40) in `LOG.md`. This is the map for phase 2.
- [x] 0.2 Create `plan/TEST_ON_DEVICE.md` (table: area, change, commit, check, failure sign).
- [ ] 0.3 Host test harness: `tests/` with a plain `g++`/`make` (or `task test`) setup, no new toolchain
      and no downloads beyond a single-header test lib if needed. Stub the few ESPHome headers the pure logic needs.
      First targets, written against **today's** behavior so later refactors are checked:
      calendar lookup and JSON parse, acid dose and K-factor update, EC dose math, pH mixing duration,
      night-mode window, grow-day calculation from a start date.
      If a function can't be tested without dragging in the controller, extract it first (that is phase 2.4 work, pull it forward).

## Phase 1: cleanup (`01-cleanup.md`)

- [x] 1.1 Delete ballast: `esptool_venv/`, `plantos-log-*.txt`, `boot_logs.txt`, `www/webserver-v2.*`,
      `ezo_switch_helper.h` (unreferenced). `wifi_scan.h` **is** included (`plantOS.yaml:22`): check what it does,
      then either keep it in a sensible place or remove include and file together.
- [x] 1.2 Delete dead components: `central_status_logger/`, `ezo_ph/`, `time_dummy/`, `psm_checker/`,
      `dummy_actuator_trigger/`, `sensor_dummy/` (+ its `external_components` entry). Remove the empty `led_behaviors/`.
      Fix the `i2c_scanner` → controller `CentralStatusLogger` include coupling if it is cheap; otherwise note it for 2.4.
- [x] 1.3 `.gitignore`: `.venv/`, `.platformio/`, `.esphome/`, log captures. `secrets.example.yaml`: make it validate.
- [x] 1.4 `Taskfile.yml`: point `decode` and `IDFMON` at the project-local `.platformio`. Add `task test`.
- [x] 1.5 `BLE-Shelly`: tag `archive/BLE-Shelly`, push the tag, delete the remote branch. Copy `huge_app.csv` into the
      notes for 2.7. Record in `LOG.md`.
- [x] 1.6 Docs: delete what is finished or contradicted by code (`MIGRATION_GUIDE.md`, `TODOFSMUNIFICATION.md`,
      `FSMUNIFICATIONPLAN.md`, `docs/fixes/*` condensed into one short note, stale performance analyses,
      `PINOUT_CHANGE.md`). Keep: `DESK_DISPLAY_FIRMWARE.md` (contract), `WEBUI_PERFORMANCE_CLEANUP_PLAN.md`,
      `MOTHERPLANT_CHAMBER_PLAN.md`, the Hetzner docs, the German control-logic docs and the CSV if they still match
      (else `docs/archive/`). `CHANGELOG.md`: keep, don't rewrite history. Fix the `PINOUT.md` header date.
- [x] 1.7 `TODO.md`: reorganize, don't delete. Drop done items and the `/home/cody` reference, keep the owner's open items
      in their wording, add a short "from the overhaul" section at the end.
- [x] 1.8 Stale pin labels (DS18B20 "GPIO23" → GPIO3, old pump GPIOs in switch comments).
- [x] 1.9 Update `AGENTS.md` and `MAP.md` to the new tree. Check `git branch -a`.

## Phase 2: lean and fast (`02-lean-fast.md`)

Order by expected gain per risk. Measure after each step and stop a line of work when it stops paying.

- [ ] 2.1 **Compile-time log level INFO** in production, DEBUG via `packages/debug.yaml`. Note: the custom code has only
      ~110 `ESP_LOGD/V` calls against ~890 `ESP_LOGI`, so most of this gain comes from ESPHome core. Measure before
      expecting much. Remove UI "verbose" switches that become no-ops in production (or move them to the debug package)
      and list them as deliberate removals.
- [ ] 2.2 **Log diet in custom code**: the 890 `ESP_LOGI` are the real string mass. Remove banner/ASCII-art/multi-line
      decoration, demote chatter to `LOGD`, merge multi-call blocks into one line, drop logs that repeat the state name.
      Keep every log that tells an operator *why* something happened (transitions, dose amounts, aborts, alerts).
- [ ] 2.3 **`CentralStatusLogger`** (1.3k lines): reduce to a compact on-demand report built into a fixed buffer.
      Decide whether the 420 easter egg stays (default: keep it tiny or drop it; list in `LOG.md`).
- [ ] 2.4 **Split and simplify the controller.** Target layout inside `plantos_controller/` (names for roles, not features):
      `controller.cpp` (setup/loop/dispatch/transitions/public API), `ph_control.cpp`, `ec_control.cpp`,
      `water_ops.cpp` (fill, empty, reservoir change, feed-fill), `health_checks.cpp`, `scheduling.cpp`
      (night mode, grow day, auto triggers), `dosing_math.h/.cpp` (pure functions, host-tested), `led/` for the behaviors.
      First move mechanically (commit), then simplify per file (separate commits, each with `TEST_ON_DEVICE.md` rows):
      shared "start operation" guard, shared mixing/settling timer, one retry helper, `std::string` → `const char*`/enum
      where it is only a label. Shrink `controller.h` to the API that is really called.
- [ ] 2.5 **YAML lambdas → C++.** Move the `on_boot` configuration (max durations, PWM sync) into component config or
      controller setup, collapse the button/switch/number lambdas to one-line calls, and remove per-lambda logging.
      Goal: no business logic left in `plantOS.yaml`, and far fewer than 110 lambdas.
- [ ] 2.6 **`ezo_ph_uart` `delay()`**: turn the command paths into a small non-blocking state machine. High risk without
      the probe attached, so it goes last in this phase, gets host tests for the parser, and detailed
      `TEST_ON_DEVICE.md` rows (calibration, temperature compensation, reading cadence). Skip it if time runs short.
- [ ] 2.7 **8 MB flash, prepared only**: partition CSV + the `flash_size: 8MB` lines as a commented opt-in, with a
      "USB flash required" note in `LOG.md` and `AGENTS.md`. Nothing else may depend on it.
- [ ] 2.8 Smaller checks, each only if the symbol map says it pays: `ArduinoJson` in the calendar (goes away with 3.2),
      `std::map` use, sdkconfig options (mbedTLS cipher suites and buffer sizes for the one MQTT broker, lwIP), unused
      ESPHome components in the YAML. The `plantos/vitals` payload must stay byte-compatible in its keys.

## Phase 3: presets (`03-presets.md`)

### Model

```
Preset (3 fixed slots: anzucht, mutter, haupt)
  tank_volume_delta_l, tank_volume_total_l      UI-editable, NVS per preset, YAML default
  functions (bit set)                           UI-editable, NVS per preset, YAML default
      NUTRIENTS    nutrient pumps A/B/C  → feeding, EC feeding, mother mix
      WATER_FILL   valve + level sensors → fill, top-up, feed-fill
      DRAIN        wastewater pump       → empty, reservoir change
      AIR_PUMP     Shelly socket 0       → air mixing (without it: settle by time only)
      GROW_LIGHT   Shelly socket 3       → light schedule
  calendar                                      YAML only, phase table
  grow start date                               NVS per preset, "restart grow" sets it to today
```

- Defaults: Haupt = all functions, 27.4 / 47.5 L, today's 120-day schedule expressed as phases, start date `2025-12-30`.
  Mutter = NUTRIENTS + WATER_FILL + DRAIN. Anzucht = none (pH only) until the owner answers.
  Placeholder volumes and calendars for Mutter and Anzucht go into `TO_MEASURE.md`.
- pH measurement and correction are always available. Night mode, flow rates and Shelly wiring stay global.

### Steps

- [ ] 3.1 **Capability gate, one place.** Each operation declares the functions it needs in a small table
      (`operation → required function mask`). The `start*()` entry points and the auto-trigger checks ask one helper,
      `canRun(op)`. If it says no: refuse, log once, raise a UI-visible reason. No `if (preset…)` inside state handlers.
      Second line of defense: the ASG rejects commands for actuators the active preset lacks.
      Open detail to settle while coding: pH mixing without AIR_PUMP uses the time-only path (longer settle time,
      placeholder → `TO_MEASURE.md`).
- [ ] 3.2 **Calendar as phases.** Replace the 120-line JSON with a YAML list per preset
      (`from_day`, pH range, doses A/B/C, light on/off, EC target/tolerance), validated in Python at build time and emitted
      as `const` arrays (no runtime JSON parse, `ArduinoJson` drops out of the calendar). Lookup = last phase with
      `from_day <= day`; past the end holds the last phase. A script check proves the Haupt phases expand to exactly
      today's 120 rows before the JSON is deleted. Host tests cover lookup and hold.
- [ ] 3.3 **Preset store** (a component named for its role, e.g. `grow_profile`): active preset index, per-preset
      volumes, function mask and start date in NVS, with YAML defaults and a version field. The HAL's
      `getTankVolumeDelta()/getTotalTankVolume()` read from it, so the dosing code does not change.
      Existing NVS day data migrates to the Haupt slot on first boot (argued from code, row in `TEST_ON_DEVICE.md`).
- [ ] 3.4 **Switching.** Allowed only in IDLE, NIGHT, SHUTDOWN, PAUSE or ERROR. During a running operation the request
      is refused with a message (no queueing). On a switch: all actuators off through the ASG, auto-trigger timers and
      retry state reset, calendar and light state re-evaluated, PSM recovery records carry the preset so a crash recovery
      never resumes an operation under a different preset. Document in `FSMINFO.md`.
- [ ] 3.5 **UI** (every entity gets a sorting group, probably a new `sg_preset`): `select` "Preset", numbers
      "Tank Volume LOW→HIGH" and "EMPTY→HIGH", five function switches, button "Restart Grow (Day 1)", text sensor with
      the active preset, day and phase. Entities whose function is off stay visible but their actions answer with the
      refusal reason. Add the preset name to logs; add it to `plantos/vitals` only as a **new** key (existing keys untouched).
- [ ] 3.6 Fold the existing "Mother Plant Nutrients" button / "Mother Mix Volume" number into the model
      (it needs NUTRIENTS; keep the feature in Haupt as today).
- [ ] 3.7 Docs: `FSMINFO.md`, `AGENTS.md`, `MAP.md`, and complete `TO_MEASURE.md` (volumes, calendar values per preset,
      settle time without air pump, function sets to confirm) with the exact UI entity or YAML line for each value.

## Phase 4: UI evaluation (`04-ui-eval.md`)

Runs in parallel as filler while builds run; finish after 3.5 so presets are in the concept.

- [ ] 4.1 `plan/ui-eval/ASSESSMENT.md`: options (a) page served from the ESP, (b) external page against REST/SSE,
      (c) stay on v3 with better grouping. Flash cost from real numbers after phase 2, offline LAN use, maintenance
      across ESPHome versions, security (check `web_server` auth and the API encryption state in the YAML). Recommendation.
- [ ] 4.2 `plan/ui-eval/prototype/index.html`: one static file, no build step, device address configurable, talks to
      the 2025.4 REST/SSE endpoints (handles events without `domain`), has a mock mode so it can be looked at without
      the device. Shows status, main actions, presets, settings.

## Wrap-up (last hours of 2026-11-16)

- [ ] Final clean build, production and debug package. Flash/RAM before → after in `LOG.md`.
- [ ] `TEST_ON_DEVICE.md` ordered as a checklist for the first flash: what to watch in the first 10 minutes, then per feature.
      Include the rollback: the commit of today's production firmware (`99a181e`) and how to flash it.
- [ ] `TO_MEASURE.md`, `QUESTIONS.md`, `AGENTS.md`, `MAP.md`, `FSMINFO.md` consistent with the code.
- [ ] `git branch -a` shows only `main`. Workstream status table in `README.md` updated.

## If time runs short

Cut in this order: 2.6 (EZO `delay()`), 2.8, the prototype in 4.2, the deeper simplification half of 2.4 (keep the
mechanical split). Never cut: phase 0, 3.1–3.5, `TEST_ON_DEVICE.md`, the final green build.
