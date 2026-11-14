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
