# Overhaul plan (Nov 2026)

Entry point for the overhaul. Read this file, then `LOG.md` (where the last session stopped),
then `IMPLEMENTATION.md` (the ordered step list with checkboxes) and only the workstream you are working on. `AGENTS.md` stays the reference for
architecture and coding rules; this folder is about **what to change and why**.

Prepared 2026-11-14 against commit `99a181e`. The sprint runs until **2026-11-16 23:59**.

## Goal

Get the repo and the firmware into good shape:

1. **Clean**: remove dead code, ballast and stale docs. In the end there is one branch, `main`, with a tidy tree.
2. **Lean and fast**: less flash, less loop time, less log noise, and smaller, understandable units.
3. **Configurable**: runtime presets (Anzucht / Mutter / Haupt) so you can switch systems without reflashing.
4. **UI evaluation**: assess a Shelly-style custom web UI. This is an evaluation and a prototype sketch, not a replacement yet.

The workstream files give the intent and the hard constraints. **How** to do it is up to you.
Challenge anything here that turns out to be wrong.

## Hard constraints

- **Build only, no flashing.** The owner is travelling and the device is out of reach.
  "Done" means it compiles (`task build`) and passes `task config_validate`. The device runs
  a small test grow with an old mother plant, so the current production firmware must keep working.
  Anything that only takes effect after a **USB** flash (partition table, flash size) can be
  prepared, but it has to be marked clearly in `LOG.md` and must not be required by the rest.
- **FSM**: `FSMINFO.md` is authoritative. Update it in the same commit whenever FSM behavior changes.
- **Secrets**: `secrets.yaml` holds the real credentials. Never print, log or commit it.
- **Disk**: the root partition has only ~1 GB free. A build uses ~350 MB in `.esphome/`.
  An ESPHome upgrade would pull a second toolchain (~2 GB) and **won't fit**. Stay on 2025.4.2.
- **Git**: you may commit and push on your own. Small, buildable commits with English
  messages work best. The end state is `main` only, with no branches left for the owner to merge.
  You can use short-lived branches, but fast-forward or merge them yourself and delete them.
  See `01-cleanup.md` for the old `BLE-Shelly` branch.

## Environment (this laptop)

```bash
cd ~/Coding/plantOS-Hydromat
source .venv/bin/activate     # ESPHome 2025.4.2 + esp-idf-monitor (uv venv, not in git)
task config_validate          # ~20 s
task build                    # ~6 min cold, much faster incremental
```

There is no Nix and no direnv on this machine. `task` and `uv` are in `~/.local/bin`. PlatformIO
lives in `.platformio/` inside the repo.

## Workstreams

| # | File | Status |
|---|---|---|
| 1 | `01-cleanup.md`, repo and code hygiene | done 2026-11-14 |
| 2 | `02-lean-fast.md`, performance, flash, structure | mostly done (2.3, 2.6 open) |
| 3 | `03-presets.md`, runtime presets | done 2026-11-15, untested on hardware (`TEST_ON_DEVICE.md`, `TO_MEASURE.md`) |
| 4 | `04-ui-eval.md`, custom web UI evaluation | done 2026-11-14 (`plan/ui-eval/`), owner feedback open |

Suggested order: 1 → 2 → 3, with 4 in parallel. Cleanup first makes everything after it
cheaper to read. Re-order if you see a better path.

## Supporting files

- `IMPLEMENTATION.md`: the ordered step list and the owner's design decisions. Tick steps off there.
- `TEST_ON_DEVICE.md` (created in phase 0): every change that needs a check on real hardware.
- `MAP.md`: where things live (a dense orientation, no prose)
- `WEAKNESSES.md`: observed problems with evidence (observations, not orders)
- `LOG.md`: an append-only session log. **Update it at the end of every session**, so the next one
  doesn't have to re-derive anything.
- `QUESTIONS.md`: open questions for the owner. Answers arrive asynchronously, so park a question
  there and continue with a sensible default instead of blocking.
