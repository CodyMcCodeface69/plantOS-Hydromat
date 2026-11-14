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
