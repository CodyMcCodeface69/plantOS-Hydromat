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
