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
