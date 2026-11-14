# Open questions for the owner

The owner answers asynchronously. Park a question here, pick a sensible default, note it in
`LOG.md` and continue. Move answered questions to the bottom section.

## Open
- **Anzucht hardware** (not answered on 2026-11-14): which of nutrient pumps A/B/C, water valve + level sensors,
  wastewater pump, air pump, grow light does the Anzucht setup have? Default until answered: none of them
  (pH probe + acid pump only). All five are switchable in the UI, so a wrong default costs five taps.

## Answered
- Presets: runtime-switchable. They differ in tank volume, calendar and active actuators. (2026-11-14)
- Custom UI: evaluation only. (2026-11-14)
- Flashing: not possible, build only. A small test grow runs with an old mother plant. (2026-11-14)
- Cleanup: deleting and archiving is allowed. (2026-11-14)
- Git: the agent decides. End state is a clean `main` with no branches left to merge. (2026-11-14)
- `plantos.pdf`: not needed, already deleted. (2026-11-14)
- Mutter/Anzucht values: the owner measures them later. Use placeholders for now and list them in `plan/TO_MEASURE.md`. (2026-11-14)
- Grow day: own start date / day counter per preset. (2026-11-14)
- Calendar end: hold the last phase; calendars are phase tables. (2026-11-14)
- Editing: tank volumes + function switches in the UI (NVS per preset), calendars in YAML. (2026-11-14)
- Refactoring: aggressive is fine, but note everything that might change behavior and needs a real-life test
  (`plan/TEST_ON_DEVICE.md`). (2026-11-14)
- Mutter hardware: nutrient pumps, water valve + level sensors, wastewater pump. No air pump, no grow light. (2026-11-14)
- Log level: INFO in production, DEBUG in the debug build. (2026-11-14)
- `BLE-Shelly`: tag + delete, keep only the 8 MB partition idea as an opt-in. (2026-11-14)
