# Resume here (paused 2026-11-15)

Short restart note. Delete this file when the wrap-up is done.

## State
- `main` is pushed and clean. All feature work of the sprint is committed (phases 0-4 of `IMPLEMENTATION.md`).
- Last verified build (production, `75fafd5`): flash 1,400,312 B (76.3 %), RAM 37,356 B, 314 host tests + 12 Python
  tests green, config valid. Sprint start: 1,583,438 B (86.3 %).
- Nothing has been flashed. The device still runs `99a181e`.
- **The build directory may hold a debug build.** Run `task build` once before trusting it.
- No agents are running. No branches besides `main`.

## What is left: the wrap-up only (docs + checks, no firmware changes)
A wrap-up agent was stopped right after it started (it had only fixed `task reboot` in `Taskfile.yml`, committed with
this file). Redo its job; the full task list is the "Wrap-up" section of `IMPLEMENTATION.md` plus:

1. Debug build check: uncomment the `debug:` line in the `packages:` block of `plantOS.yaml`, `task config_validate`,
   `task build`, note flash/RAM, restore the line, production `task build` again. Never commit the line enabled.
2. `AGENTS.md`: make it true for HEAD (sprint banner, controller file list, `CentralStatusLogger` = alert book + I2C
   scan, `ezo_ph_uart` blocking map, "WiFi Scan" moved to the debug package, YAML line index, entity counts, known issues).
3. `plan/MAP.md`: rewrite to the current tree.
4. `plan/TEST_ON_DEVICE.md`: reorder into a first-flash checklist (before flashing, first 10 minutes, first day,
   active checks by feature, stop-and-roll-back signs). Keep every row's content.
5. `plan/TO_MEASURE.md`, `plan/QUESTIONS.md` (order by importance, EC dosing bug first), `plan/WEAKNESSES.md`,
   `plan/README.md` (status table + result summary), tick the wrap-up boxes in `IMPLEMENTATION.md`.
6. `FSMINFO.md` consistency pass against the code for what the sprint changed.
7. `TODO.md`: fill only the empty section "From the Nov 2026 overhaul".
8. Grep for leftovers: `schedule_json`, `grow_start_date`, `enhanced_error_handling`, `on_boot`, `/home/cody`,
   `http_request`, deleted doc names.
9. Final entry in `plan/LOG.md`. Three commits were never summarized because their agent's report was lost: read
   `git show 652266c` (PSM recovery event names), `155af76` (status report rewrite), `4caad97` + `75fafd5`
   (`ezo_ph_uart` periodic read non-blocking) and describe them there.

## How to work (token-saving)
- Read only: this file, the last three entries of `plan/LOG.md`, and the file you are editing.
- Subagents: use Opus (`model: "opus"`), one at a time. After an interruption RESUME the same agent with SendMessage;
  never start a replacement (two agents once edited the same files).
- Environment: `source .venv/bin/activate && export PATH=$HOME/.local/bin:$PATH`; `task test`, `task config_validate`,
  `task build`. After adding a `.cpp`: `touch .esphome/build/plantos/src/CMakeLists.txt .esphome/build/plantos/CMakeLists.txt`.
- Never print `secrets.yaml`. Build only, no flashing.

## Waiting for the owner (details in `plan/QUESTIONS.md`)
EC dosing formula is inverted (not fixed) · security (no OTA password / API encryption) · Anzucht hardware set ·
per-preset K-factors and auto switches · custom UI direction (`plan/ui-eval/prototype/index.html?mock`).
