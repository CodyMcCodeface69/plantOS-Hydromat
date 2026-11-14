# 03 Runtime presets

## Intent
One device and one firmware. Switching between **Anzucht** (propagation), **Mutter** (mother plant) and
**Haupt** (main grow) happens at runtime from the UI, without a reflash. A switch should be quick and safe.

## Decided with the owner
- **Runtime-switchable**, persisted across reboots (NVS).
- A preset differs in:
  1. **Tank volume** (delta LOW→HIGH and total EMPTY→HIGH)
  2. **Its own calendar** (day plan: pH range, nutrient doses A/B/C, EC target, light times. The length may differ)
  3. **Active actuators and functions** (e.g. a setup without nutrient pumps or without a wastewater pump,
     maybe pH correction only). The FSM must not start operations that need hardware the preset lacks.
- These stay **global** (not per preset): night mode, pump calibration (flow rates), Shelly IP and wiring.
  Change this only with a good reason.
- The owner fills in real values later. Ship sensible defaults: Haupt = the current production
  values (27.4 / 47.5 L, the current 120-day schedule). Mutter and Anzucht are plausible placeholders,
  clearly marked as such.

## Open design space (your call)
- Where presets and calendars are defined (YAML at build time + a runtime choice, editable on the device, both).
- Calendar representation (phases or ranges instead of 120 copied lines?) and its flash and NVS cost.
- What happens on a switch while an operation runs, and what happens to the grow day or start date per preset.
- How "active functions" are modelled without spreading `if`s across the FSM.
- Relation to `docs/MOTHERPLANT_CHAMBER_PLAN.md` (a second chamber at the same time). This is **not** the
  same as a preset; that doc stays a later idea. Don't block it, but don't build it either.

## Done when
- The preset switch is in the UI and the value survives a reboot (argued from code, since no flash is possible).
- Tank volume, calendar and the actuator set follow the active preset. Haupt reproduces today's behavior.
- `FSMINFO.md` and `AGENTS.md` describe the preset model.
