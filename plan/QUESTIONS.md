# Open questions for the owner

The owner answers asynchronously. Park a question here, pick a sensible default, note it in
`LOG.md` and continue. Move answered questions to the bottom section.

## Open
- **Anzucht hardware** (not answered on 2026-11-14): which of nutrient pumps A/B/C, water valve + level sensors,
  wastewater pump, air pump, grow light does the Anzucht setup have? Default until answered: none of them
  (pH probe + acid pump only). All five are switchable in the UI, so a wrong default costs five taps.

- **EC dosing bug** (found 2026-11-14, verified in `controller.cpp` `handleEcCalculating`): the adaptive EC factor is
  stored as mL/L per µS/cm, but the dose is computed as `delta_ec / K` instead of `delta_ec * K`. The result is huge and
  gets capped, so **every EC feeding step doses the full calendar amount** and the learned factor does nothing. A plain
  fix is not safe either: the default (0.00015) and the clamp range (max 0.0005) look about 5-10x too low for a 3-part
  nutrient (roughly 2 mL/L per mS/cm), so a "correct" formula would dose tiny amounts over many retries.
  Question: what do your logs show per EC step (dose in mL/L and EC rise)? Default until answered: **behavior unchanged**
  (the system has been growing with it), the bug is documented and pinned by a host test.
- **Soft-start**: the ASG PWM ramp is never configured, so pumps switch on at once. Was that deliberate? Default: leave off.

- **Custom UI direction**: open `plan/ui-eval/prototype/index.html?mock` (5 minutes). Is this the direction? If yes:
  serve it from the ESP at `/app` next to the ESPHome page (recommended, about +21 kB flash), or replace the ESPHome page?
  Default: nothing is built into the firmware during this sprint (evaluation only).
- **Security**: today there is no OTA password, no API encryption and no web auth; action URLs also accept GET with
  `Access-Control-Allow-Origin: *`, so anyone (and any web page opened) inside the LAN can trigger actions or flash.
  Set an OTA password and API encryption with the next flash? Default: unchanged for now (a new OTA password must not
  lock out the first OTA after the sprint); `web_server: auth:` would rule out any externally opened page.

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
