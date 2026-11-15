# Session log

Append-only, newest entry at the bottom. Each entry covers what was done, the current state (does the build
pass? flash %?), the decisions taken and where the next session should start.

---

## 2026-11-14: preparation (Opus)
- Cloned the repo and set up the toolchain on the laptop: `.venv` (uv, Python 3.12, ESPHome 2025.4.2,
  esp-idf-monitor), `task` 3.44.1, and PlatformIO in `.platformio/`.
- `task build` is green on `99a181e` with the owner's real `secrets.yaml`. Flash 86.3 %, RAM 12.0 %, ~6 min cold.
- Created this `plan/` folder. Updated `AGENTS.md` (git rules, environment, pointer to `plan/`).
- **Next:** start with `01-cleanup.md`.

## 2026-11-14: implementation plan (Fable)
- Asked the owner the open design questions; answers are in `QUESTIONS.md` and at the top of `IMPLEMENTATION.md`.
- Wrote `plan/IMPLEMENTATION.md`: the ordered step list (phases 0-4 + wrap-up) with checkboxes. No code changed yet.
- Findings while surveying: `wifi_scan.h` is included by `plantOS.yaml:22` (not ballast as such); the custom code has
  ~890 `ESP_LOGI` but only ~110 `ESP_LOGD/V`, so the INFO compile level alone will save less than hoped and the log
  diet (2.2) matters more.
- One open question parked: Anzucht hardware set (default: pH only).
- **Next:** phase 0 in `IMPLEMENTATION.md` (baseline numbers, `TEST_ON_DEVICE.md`, host test harness), then phase 1.

## 2026-11-14: phase 0 baseline + phase 1 code cleanup (Fable, subagents)
Work is split across subagents with path ownership; only one agent builds at a time (one build dir, ~1 GB disk).
- **Baseline `5f549e0`:** flash 1,583,438 / 1,835,008 B (86.3 %), RAM 39,256 B. After cleanup (`61d4fff`): identical
  (nothing deleted was linked). Sections: `.flash.text` 1,092,758, `.flash.rodata` 379,440.
- **Where the flash goes** (linker map, bytes): libnet80211 180k, libmbedcrypto 121k (`ecp_curves` 35k), **`main.cpp.o` 109k
  (`setup()` 48k, the generated YAML entities + lambdas)**, libpp 97k, `web_server.cpp.o` 96k + `INDEX_GZ` 77k, lwip 87k,
  `controller.cpp.o` 67k, wpa_supplicant 64k, libc 50k, mdns 32k, mqtt 20k, `hal.cpp.o` 18k, `CentralStatusLogger` 16k.
  Consequence: step 2.5 (YAML lambdas/entities) and sdkconfig (2.8) are worth more than the controller log diet.
- Done (1.1-1.5, 1.8): ballast deleted (`esptool_venv/`, log captures, `www/`, `ezo_switch_helper.h`); six dead components
  deleted; `.gitignore`; `secrets.example.yaml` validates (placeholder BSSID); Taskfile `decode`/`IDFMON` use the
  project-local PlatformIO; stale pump GPIO comments fixed; `plan/TEST_ON_DEVICE.md` created.
- Decisions: `wifi_scan.h` is needed by the "WiFi Scan" button → moved to `includes/`. `i2c_scanner` → controller
  `CentralStatusLogger` coupling is real (hardware status callback); left for 2.3/2.4.
- `BLE-Shelly`: tagged `archive/BLE-Shelly` (`224c669`), remote branch deleted. Nothing carried over. Its `huge_app.csv`
  (8 MB, 2×3 MB app, nvs 0x9000/0x5000) was never referenced by the YAML; 2.7 compares it with ESPHome's generated layout
  before preparing the opt-in (NVS position matters: grow day, K-factors, PSM).
- Noted for later: the "WiFi Scan" button blocks the loop for seconds (candidate for the debug package);
  `secrets.example.yaml` lists keys the YAML never reads; `task reboot` posts to a possibly stale entity URL.
- Running: host tests (0.3), docs cleanup (1.6/1.7), UI evaluation (4), config-level flash savings (2.1/2.7/2.8).

## 2026-11-14: docs cleanup (1.6, 1.7, 1.9) and findings (Fable, subagents)
- `docs/` 10,647 → 3,159 lines (1,626 current + archive). New: `docs/README.md` (index + trust), `docs/CONTROL_LOGIC.md`,
  `docs/LESSONS.md`. `PINOUT.md` rewritten against the YAML (old max durations and "relay board" were wrong).
  `TODO.md` 619 → 252 lines, open items kept in the owner's wording. `AGENTS.md`, `MAP.md`, `WEAKNESSES.md` updated.
- **Findings, not fixed (owner questions parked):** EC dose formula inverted → every EC step doses the full calendar
  amount; ASG soft-start never configured.
- To fix with the controller work (2.4): `FSMINFO.md` says PH_MIXING 120 s but the code scales 30-600 s; stale comments
  in `controller.cpp/.h` cite deleted or archived docs and `/home/cody/...`; `controller.h:952` comment contradicts the function.
- For presets (3.3): pH dosing uses `getTankVolumeDelta()` (27.4 L), not the total volume.

## 2026-11-14: UI evaluation (4.1, 4.2) (Fable, subagent)
- `plan/ui-eval/ASSESSMENT.md`, `README.md`, `prototype/index.html` (68 kB raw, 21 kB gzipped, mock mode, no CDN). `d291ae6`.
- Recommendation: serve the page from the ESP at `/app` beside the v3 page (small custom handler on `web_server_base`,
  about +21 kB); until a flash, the same file opened locally works against today's firmware. No Hetzner hosting
  (mixed content, offline, incompatible with web auth).
- Verified in the 2025.4.2 source: `js_include`/`css_include` only serve `/0.js`/`/0.css`; `local: true` serves
  `INDEX_GZ` (76,654 B) at `/`; all responses carry `Access-Control-Allow-Origin: *`; only simple cross-origin requests
  work; action URLs do not check the HTTP method; no web auth, API encryption or OTA password is configured.
- Object id rule: lower-case, space → `_`, other chars outside `[a-zA-Z0-9_-]` → `_`, hyphens kept, no collapsing.
- Tested only against a local fake of the API and in mock mode (dark theme looked at, light theme not).
- Inputs for 3.5 added to `IMPLEMENTATION.md` (entity names, status text with pH band/EC target, "Last Refusal").

## 2026-11-14: host tests (0.3) (Fable, subagent)
- `tests/`: 155 tests / 1231 checks against the real, unmodified component sources (calendar, controller math and
  handlers, `RobustAverager`, safety gate). `task test`: ~15 s clean, ~1 s incremental. `af54e6e`, `932ae66`, `a711e01`.
- Stubs in `tests/stubs/` stand in for ESPHome/FreeRTOS; private members are reached with `-fno-access-control`.
  ArduinoJson is copied once from `.esphome/build/plantos/.piolibdeps/`, so a checkout that never ran `task build` stops
  with a message.
- `tests/golden/haupt_schedule.csv`: all 120 days. The phase-table rewrite (3.2) must reproduce it exactly.
- Suspect behavior found and pinned (not fixed): see `WEAKNESSES.md`, last section. The auto-feed marker lost on reboot
  is the one with a real-world effect (a second feed the same day after a reboot); fix it in the simplification half of 2.4.
- **Running:** the mechanical controller split (2.4) in a worktree branch, verified by the host tests only; the
  coordinator merges and runs `task build` once the config agent (2.1/2.7/2.8) releases the build directory.

## 2026-11-14: config-level flash savings (2.1, 2.7, 2.8) and controller split merged (Fable + Opus subagents)
- **Flash 1,583,438 → 1,440,176 B (86.3 % → 78.5 %), RAM 39,256 → 37,356 B.** Debug build: 1,494,370 B, fits.
  | Measure | Flash after | Commit |
  |---|---|---|
  | Log level INFO in production (inline package, debug package raises to DEBUG) | 1,557,132 | `f4b96ed` |
  | Unused `http_request` + two scripts removed | 1,551,378 | `3870dcc` |
  | mbedTLS: client only, unused curves, static ECDH, PEM write, CRL/CSR off | 1,520,542 | `c63a159` |
  | WiFi Enterprise/OWE/SAE-PK/softAP SAE off, MQTT WebSocket transports off | 1,498,840 | `ea53ffa` |
  | Silent assertions | 1,440,176 | `a81d478` |
- TLS left enabled: TLS 1.2 client; ECDHE-ECDSA, ECDHE-RSA, RSA; secp256r1/384r1/521r1 + Curve25519; AES-GCM/CCM/CBC;
  SHA-1/256/384/512. Fallback AP, OTA, native API, web_server unchanged. Rows are in `TEST_ON_DEVICE.md`.
- Checked and dropped (no or tiny gain, or unsafe): mbedTLS error strings, secp521r1, nano printf (64-bit formats in use),
  lwIP, unused components (none). Not done, owner question: `ESP_ERR_TO_NAME_LOOKUP` off (~7.5 KB).
- Removed entity: debug-only switch "Verbose: HTTP Request". Production entities unchanged.
- **8 MB prepared, NOT enabled, USB flash required:** commented `flash_size: 8MB` + `partitions: partitions_8mb.csv` in
  `plantOS.yaml`. The custom table keeps otadata/phy_init/app0/nvs at today's offsets (nvs 0x390000, 436 KB) and gives
  two 3584 KB app slots. ESPHome's own 8 MB layout would move NVS to 0x790000 and wipe grow day, K-factors, calibration.
- **Controller split merged** (`f6039f4`): `controller.cpp` 4854 → 1711 lines + `ph_control`, `ec_control`, `water_ops`,
  `scheduling`, `health_checks`, pure math in `dosing_math`/`schedule_math`. 178 host tests green. Body diff: 85/85
  functions, only the 18 that call an extracted function differ. Firmware build green: flash 1,441,132 B (+956 B).
- Lesson: two agents ran on the same task for a while after a usage-limit interruption (the old ones resumed on their
  own). After a limit, resume the existing agent with a message; do not start a replacement.
- Lesson: new `.cpp` files need a CMake reconfigure (see `AGENTS.md` known issues).
- **Running:** controller simplification (dead code, auto-feed/reservoir marker fixes, log diet, handler restructuring)
  on the worktree branch `worktree-agent-a760c26e111b63836`. **Next:** merge it + build, then 2.5 (YAML lambdas → C++),
  2.3 (`CentralStatusLogger`), then phase 3.

## 2026-11-14: controller simplification merged (2.2 controller part, 2.4) (Opus subagent)
- Merged `95471da`. **Build green: flash 1,425,614 B (77.7 %), RAM 37,308 B.** Host tests 197 / 1621 checks.
- Lines: controller.cpp 1711 → 1348, ph_control 1152 → 795, ec_control 718 → 657, water_ops 587 → 346,
  scheduling 318 → 231, health_checks 312 → 243, controller.h 1206 → 1075.
- Dead code out: grow-day calculation from `grow_start_date` (never had a caller; the calendar's `sync_to_date` is what
  counts days), retry bookkeeping (also a slow heap leak), unread sensor tiers, unused getters. `grow_start_date` and
  `enhanced_error_handling` are still accepted in YAML but ignored (clean up with 2.5).
- Deliberate fixes (rows in `TEST_ON_DEVICE.md`, `FSMINFO.md` updated): auto-feed / auto-reservoir "done" markers in
  their own NVS keys (`AutoFeedDay`, `AutoResWeek`); reservoir weekday in local time; air pump tracked state.
- Restructured with tests written first: pH calibration (table-driven), `loop`/`handleIdle` parts, water ops helpers,
  one `canStart(Operation, &reason)` for all start actions (**the hook for the preset capability check, 3.1**).
- Log diet: 489 → 179 INFO/WARN/ERROR calls in the controller.
- **Left open:** WATER_FILLING / WATER_EMPTYING open the valve/pump only in the first 100 ms of the state and use a
  `*_cmd_sent_` flag that stays set after an emergency stop/pause: a late first call or an interrupted fill means the
  next fill never opens the valve and "times out". To fix with `state_entry_executed_` in the next step.
  EC formula and double accumulation untouched (owner question).
- **Next:** 2.5 YAML lambdas → C++, 2.3 `CentralStatusLogger`, the fill/drain entry-gate fix; then phase 3.

## 2026-11-15: YAML lambdas → C++ (2.5), fill/drain entry fix, calendar phase tables (3.2) (Opus subagents)
- **Build green at the merge of both: flash 1,381,070 B (75.3 %), RAM 37,036 B.** Host tests 232. Debug build links.
  Start of sprint: 1,583,438 B (86.3 %).
- 2.5: flash 1,425,590 → 1,407,722. YAML 2,711 → 2,002 lines, `ESP_LOG` in YAML 227 → 3, lambdas 113 → 92 (each one call).
  `on_boot` lambda gone: max durations under `actuator_safety_gate: max_durations:`, air pump cycling periods, PWM
  sliders bound through `plantos_hal`, K-factor number through `plantos_controller`. New
  `plantos_controller/operator_api.cpp` holds the logic of the manual switches, pump/valve tests, day change and the
  text getters (host-tested). `grow_start_date` and `enhanced_error_handling` keys removed.
- **3-layer rule violations fixed:** the eight pump test buttons went straight to the HAL (Emergency Stop could not stop
  them); manual GPIO/Shelly switches and the valve test skipped the controller. All go Controller → ASG → HAL now.
- Deliberate removals from production: "WiFi Scan" button (blocks the loop; now in the debug package with its include).
- Fill/drain entry fix (`9a7c5a0`): WATER_FILLING/EMPTYING open once per state entry via `state_entry_executed_`.
- 3.2 (`bf6659b`): `calendars:` with phase lists per preset; Haupt = 18 phases, proven equal to the 120-row golden table
  two ways. Per-calendar NVS slots, migration of the old day into Haupt, old keys left in place (rollback safe).
  Day counter is `uint16_t`, no wrap at 120, last phase holds. Placeholders for Mutter/Anzucht are in `TO_MEASURE.md`.
  `CalendarManager::set_active_calendar()` exists but nothing calls it yet (3.3).
- **Left for 3.3:** `uint8_t` day copies at `ph_control.cpp` (~246), `controller.cpp` (~277), `ec_control.cpp` (~38),
  the "Day %d / 120" text in a YAML lambda / `operator_api` and in `CentralStatusLogger.cpp` (~470).
- Owner note: the phase names from `docs/Hydroponik_Zielwerttabelle.csv` are shifted against the schedule values
  (days 78-98 carry "Späte Blüte" values under the "Mittlere Blüte" weeks). Labels only.
- **Next:** 3.1 + 3.3-3.7 (preset store, capability gate in `canStart`, switch, UI), then 2.3, 2.6 if time, wrap-up.

## 2026-11-15: presets (3.1, 3.3-3.7) (Opus subagent)
- Commits `4dda0eb`, `8521ee2`, `d0ad899`, `24757cd`. **Build green: flash 1,409,922 B (76.8 %), RAM 37,356 B.**
  Presets cost +28.9 KB (about 5 KB logic, the rest the new `select` and `script` ESPHome components). Host tests 274;
  no pre-existing expectation changed (Haupt = today).
- Model: store inside `plantos_controller` (`preset_model.h`, `grow_profile.*`, `presets.cpp`). NVS: `preset_active`,
  `preset_slot_0..2` (written only on change; once a preset is edited in the UI its NVS record wins over YAML defaults).
  YAML defaults under `plantos_controller: presets:`; tank volumes left the `plantos_hal` block.
- Capability table in `operationFunctions()`, checked in `canStart()`; auto triggers use the same table silently; the
  safety gate blocks ON for actuators the preset lacks. Without AIR_PUMP the mixing time is × `no_air_pump_settle_factor`
  (2.0, placeholder). Feed/reservoir "done" markers are per preset (Haupt keeps the old keys).
- Switch: only in IDLE/NIGHT/SHUTDOWN/PAUSE/ERROR; index persisted first; dosing pumps, valve, drain pump off; air pump
  and light off only if the new preset lacks them; old-tank state (EC cycle, 12 h guard, pending checks, plausibility
  history) reset. K-factors and auto switches stay global (owner questions).
- UI: group `sg_preset` with select "Preset", two tank volume numbers, five function switches, "Restart Grow (Day 1)",
  "Preset Status"; "Last Refusal" in status. `plantos/vitals` gained `"preset"` as the last key (consumers read by name).
- Placeholders: Mutter 10 / 15 L, Anzucht 5 / 8 L, function sets, settle factor → `TO_MEASURE.md`.
- Side finding, not fixed yet: `startFillTank` logs the PSM event `WATER_FILL` but recovery checks `WATER_FILLING`.
- **Next:** 2.3 `CentralStatusLogger`, the PSM name fix, 2.6 only if it can be done safely; then the wrap-up.

## 2026-11-15: session paused by the owner (Fable)
- Committed by the last subagent before it was stopped (its final report was never delivered, so the details below are
  from the commit log only): `652266c` PSM recovery uses one set of event names (a crash during fill/drain/feed/reservoir
  change now stops valve and drain pump); `155af76` status report rewrite (`CentralStatusLogger` is only the alert book
  + I2C scan); `4caad97` `ezo_ph_uart` periodic read no longer blocks the main loop, with host tests; rows in
  `TEST_ON_DEVICE.md`; 2.3 and 2.6 ticked.
- The agent was stopped during its final checks with the debug package line enabled in `plantOS.yaml`; that edit was
  reverted (`git checkout plantOS.yaml`). **The build directory may hold a debug build or a half-finished build.**
- **Not verified in this state:** production `task test` / `task config_validate` / `task build` on HEAD, the debug
  build, flash/RAM numbers after 2.3 and 2.6.
- **Next session, in this order:** (1) `task test`, `task config_validate`, `task build` on `main`, record flash/RAM;
  (2) review the three commits above (`git show --stat`), especially `4caad97`: the pH probe driver is the riskiest change
  of the sprint and has no hardware test; decide whether its TEST_ON_DEVICE rows are sufficient; (3) update `AGENTS.md`
  (`CentralStatusLogger` description, the `delay()` note in the coding rules, the "WiFi Scan" include line, entity count)
  and `MAP.md`; (4) wrap-up section of `IMPLEMENTATION.md`: debug build, order `TEST_ON_DEVICE.md` as a first-flash
  checklist with the rollback, check `TO_MEASURE.md` / `QUESTIONS.md` / `FSMINFO.md`, workstream table in `README.md`.
