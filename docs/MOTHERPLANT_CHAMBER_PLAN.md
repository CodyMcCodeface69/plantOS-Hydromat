# Minimal Second Chamber (Mother Plant) — pH Correction Only

> **Deliverable for now:** save this as a design doc in the repo
> (`docs/MOTHERPLANT_CHAMBER_PLAN.md`) for **later** implementation. Do **not** write code,
> edit `plantOS.yaml`, or add components in this pass.

## Context

TODO.md Phase 3 ("More Chambers", lines 366–488) sketches a full dual-chamber build:
2× controller + 2× HAL + 2× SafetyGate, 14 actuators, multiplexed pH sensors — 50–78 h,
not started. The two chambers there are the **main** plant and the **mother plant**. This
plan delivers a **minimal slice**: a second (mother-plant) chamber that does **pH correction
only**, with **one acid pump** and **one cheap analog pH probe (PH4502C)**.

Decisions locked with the user:
- **New, minimal controller component named `motherplant_controller`** — a deliberately
  broad name so future mother-plant features (lighting, feeding, etc.) can be added to the
  same component without a rename/refactor. It is *not* a second instance of the 4700-line
  `plantos_controller` (which is coupled to EC/feeding/water/calendar and assumes a Shelly
  air pump during mixing).
- **HAL and ActuatorSafetyGate are SHARED** — the chamber-2 acid pump is a new actuator ID
  routed through the *existing* `hal` and `actuator_safety` instances. No second HAL/ASG.
- **Mixing = passive settle** — `PH_MIXING` is a timed wait with no actuator (this chamber
  has no air pump), then re-measure.

Intended outcome (once implemented): trigger "Mother Plant: pH Correction" from the web UI;
the new FSM measures pH (analog), doses acid through the shared safety gate, waits to settle,
and re-checks until in range — fully independent of the main chamber's logic.

## Hardware / Pin Plan (ESP32-C6)

Pins currently used (from `plantOS.yaml`): 0,1,3,4,5,6,7,8,17,18,19,20,21,22,23.
Free: 2,9,10,11,12,13,14,15,16. Strapping pins to avoid: GPIO8/9/15.

| Signal | Pin | Notes |
|--------|-----|-------|
| PH4502C **Po** (pH analog out) | **GPIO2** | Only free ADC1 channel (ADC1 = GPIO0–6; GPIO2 is the lone unused one). |
| Mother-plant acid pump (LEDC PWM → relay/driver) | **GPIO11** | Mirrors the main chamber's GPIO4 pattern. Non-strapping. |

- PH4502C is powered at 5 V; its `Po` can exceed 3.3 V at extreme pH. Use a divider (or the
  module's reference-adjust pot) to keep the signal ≤ ~3.1 V into the ADC, then calibrate.
- `To` (temp comp) and `Do` (level/threshold) outputs are **not wired** in this minimal build;
  the probe's onboard temperature compensation is sufficient — V→pH conversion uses no
  software temp comp.
- Acid pump driver defaults to **GPIO LEDC PWM** (user emphasized actuators flow through the
  shared HAL+ASG; this is the on-board path). Swapping to a Shelly/HTTP socket later only
  changes the HAL routing for the `AcidPumpMother` ID, not the controller.

## Analog pH Signal Chain (in `plantOS.yaml`)

Reuses the already-present `leon-v/esphome` `adc_one_shot_C2` fork (external_components,
~line 261) — the only ADC that compiles on ESP32-C6.

1. `adc` platform raw voltage sensor on **GPIO2** → `ph_mother_adc_raw` (`internal: true`,
   `update_interval: 100ms`, `samples: 5`). Model after the existing TDS ADC block
   (`plantOS.yaml` ~468–474).
2. `template` sensor → `ph_mother_raw`: linear V→pH `pH = slope * V + intercept`, clamp 0–14.
   Slope/intercept are the calibration constants (two-point: e.g. pH4 & pH7 buffers).
3. `sensor_filter` platform → `ph_mother_filtered` (`sensor_source: ph_mother_raw`,
   `window_size: 5`, `reject_percentage: 0.10`) — reuses the existing `sensor_filter`
   component, same config style as the main chamber's `sensor_filtered_id` (~493–509).

`ph_mother_filtered` is the sensor the HAL exposes to the new controller.

## Shared HAL Extension

Files: `components/plantos_hal/hal.h`, `hal.cpp`, `__init__.py`.

The project doctrine is "all hardware through HAL" (Layer 3), so the mother-plant probe and
pump are wired into the **existing** `hal` instance rather than read directly by the
controller.

- **hal.h** — add to the abstract `HAL` (non-pure, default returns, so nothing else breaks),
  override in `ESPHomeHAL`:
  ```cpp
  virtual float readPHMother() { return 0.0f; }
  virtual bool  hasPhValueMother() const { return false; }
  ```
  Add members `esphome::sensor::Sensor* ph_sensor_mother_{nullptr};`
  and `esphome::output::FloatOutput* pump_acid_mother_output_{nullptr};`
  plus setters `set_ph_sensor_mother()`, `set_pump_acid_mother_output()`.
- **hal.cpp** — implement the two overrides off `ph_sensor_mother_->has_state()/state`; in the
  existing `setPump()` string-routing block (~284–341) add a case:
  `else if (pumpId == "AcidPumpMother") pump_acid_mother_output_->set_level(state ? pwm : 0.0f);`
- **__init__.py** — add optional `CONF_PH_SENSOR_MOTHER` (`cv.use_id(sensor.Sensor)`) and
  `CONF_PUMP_ACID_MOTHER_OUTPUT` (`cv.use_id(output.FloatOutput)`); in `to_code` call the
  setters when present.

**ActuatorSafetyGate**: no code change. `executeCommand("AcidPumpMother", true, dur)`
validates and forwards to `hal->setPump("AcidPumpMother", ...)`. Register its limit in
`on_boot`: `id(actuator_safety)->setMaxDuration("AcidPumpMother", 30);`
*(Verify during impl that ASG forwards the actuator-ID string to `hal->setPump` — confirm in
`components/actuator_safety_gate/ActuatorSafetyGate.cpp` executeCommand actuation path.)*

## New Component: `motherplant_controller`

New dir `components/motherplant_controller/` modeled on `plantos_controller` but stripped to
pH only (broad name leaves room for future mother-plant features). **No LED behavior system**
(status via `ESP_LOG` only), **no calendar/PSM**, **no EC/feeding/water states**, **no
PH_CALIBRATING** (analog probe is calibrated by the V→pH constants + module pots, not by
firmware commands).

`__init__.py` (model on `plantos_controller/__init__.py`):
- Namespaces/`use_id` for `plantos_hal::HAL` and `actuator_safety_gate::ActuatorSafetyGate`.
- Required: `hal`, `safety_gate`. Optional config with defaults:
  `actuator_id` ("AcidPumpMother"), `tank_volume_l`, `ph_target_min`, `ph_target_max`,
  `ph_correction_target`, `dose_k`, `acid_flow_ml_per_s`, `measure_duration` (30s),
  `mixing_duration` (or volume-derived), `max_attempts` (5).

`motherplant_controller.h/.cpp` — enum `MotherState { INIT, IDLE, ERROR, PH_MEASURING,
PH_CALCULATING, PH_INJECTING, PH_MIXING }`, switch-dispatch `loop()`, non-blocking `millis()`
timing, `transitionTo()`. Public API: `startPhCorrection()`, `measurePhOnly()`,
`getCurrentState()`. Handlers (port simplified logic from `controller.cpp`):
- **PH_MEASURING** — ensure acid pump off, collect `hal_->readPHMother()` samples for
  `measure_duration`, robust-average → PH_CALCULATING (ERROR if `!hasPhValueMother()`).
- **PH_CALCULATING** — in range → IDLE; below min → log/IDLE (acid can only lower pH);
  above max → `dose_ml = (pH - ph_correction_target) * tank_volume_l * dose_k`, clamped;
  `duration_ms = dose_ml / acid_flow_ml_per_s * 1000` → PH_INJECTING; stop at `max_attempts`.
  (Port of `calculateAcidDoseML()`, controller.cpp ~3899; adaptive-K learning omitted —
  fixed `dose_k` constant, note as future enhancement.)
- **PH_INJECTING** — on entry `safety_gate_->executeCommand("AcidPumpMother", true, dur_sec)`;
  after duration `executeCommand("AcidPumpMother", false)` → PH_MIXING.
- **PH_MIXING** — **passive**: wait `mixing_duration` (no actuator) → PH_MEASURING (loop).

## `plantOS.yaml` Wiring

- Add the 3 sensors (adc raw / template / filter) and 1 `ledc` output + wrapping `switch`
  (turn_on/off → `actuator_safety->executeCommand("AcidPumpMother", ...)`, mirroring the
  `pump_ph_switch` block ~1990–2004).
- Extend the existing `plantos_hal:` block (~1407) with
  `ph_sensor_mother: ph_mother_filtered` and `pump_acid_mother_output: pump_acid_mother_output`.
- Add `motherplant_controller:` block: `id: controller_mother`, `hal: hal`,
  `safety_gate: actuator_safety`, plus mother-chamber config values.
- `on_boot`: `setMaxDuration("AcidPumpMother", 30)`.
- Web UI: buttons "Mother Plant: Measure pH" → `controller_mother->measurePhOnly()` and
  "Mother Plant: Start pH Correction" → `controller_mother->startPhCorrection()`; a manual
  "Mother Plant Acid Pump" switch; expose `ph_mother_filtered`; add a `text_sensor` for the
  mother-plant controller state.

## Docs

Add a "Mother Plant (pH-only) Controller" section to **FSMINFO.md** (per CLAUDE.md's
update-on-FSM-change rule) documenting the 7-state diagram, triggers, and the shared
HAL/ASG actuator path. Note the new component in CLAUDE.md's component reference.

## Verification (for the future implementation pass)

1. `task build` — confirm the new component + HAL changes + `motherplant_controller` compile
   (watch for the ESP32-C6 ADC fork rebuild; pyserial warning is harmless).
2. `task run` and via web UI / serial logs:
   - **pH read**: dunk PH4502C probe in pH7 then pH4 buffer; confirm `ph_mother_filtered`
     tracks and lands near 7.0 / 4.0 after setting slope/intercept (two-point calibrate).
   - **Acid pump path**: toggle the manual "Mother Plant Acid Pump" switch; confirm GPIO11
     drives the pump and ASG logs accept/reject (including >30 s rejection).
   - **Full FSM**: with probe reading above `ph_target_max`, press "Start pH Correction";
     watch state log INIT→…→PH_MEASURING→PH_CALCULATING→PH_INJECTING→PH_MIXING→PH_MEASURING,
     a clamped dose, a timed passive-settle mixing wait, and convergence into range / IDLE
     (or stop at `max_attempts`).
   - Confirm the main chamber's controller is unaffected (its states/actuators independent).

## Out of Scope (future)

Adaptive K-factor learning for the mother chamber, a status LED, periodic auto-correction
scheduling, temperature compensation wiring (`To`), and the full Phase-3 feeding/water/EC
stack — all of which can be added to `motherplant_controller` later without renaming it.
