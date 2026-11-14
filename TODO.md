# PlantOS TODO List

Maintained by hand by the owner. Reorganized on 2026-11-14: items that are done in the code were
removed (list at the end), the open items keep their original wording, and anything that could not
be settled from the code is under "Check: still relevant?". The version before the
reorganization is in git (`git show 5f549e0:TODO.md`).

---

## Open

### P2-5: Data Export (CSV Logging)
**Status**: Open
**Priority**: P3
**Effort**: 8-12 hours

**Features**:
- SD card component (SPI)
- CSV format: timestamp, pH, temp, state, events
- Log rotation (daily files, 30-day retention)

**Files to modify**:
- `plantOS.yaml`: Add SD card component
- Create new component: `csv_logger` or use lambda

> Note 2026-11: `docs/DESK_DISPLAY_FIRMWARE.md` plans this on the separate desk-display ESP32
> (it has the SD slot and reads `plantos/vitals`), not on the controller.

---

### Deferred Tasks (Future Enhancements)

#### Task #12: Mobile-Friendly Dashboard
**Priority**: Low
**Effort**: 16-24 hours
**Reason**: Web UI sufficient for MVP, nice-to-have for later

#### Task #13: Advanced pH Control (PID)
**Priority**: Low
**Effort**: 16-24 hours
**Reason**: Simple threshold control works well, PID adds complexity

#### Task #15: User Manual
**Priority**: Low
**Effort**: 8-12 hours
**Reason**: Post-production documentation, after system is stable

#### Task #16: API Documentation
**Priority**: Low
**Effort**: 4-8 hours
**Reason**: Post-production documentation, after API is finalized

---

## More Chambers (Multi-Chamber Support)

**Goal**: Control two independent growth chambers (main + mother plant)
**Timeline**: 50-78 hours
**Status**: Not started

> Note 2026-11: two newer plans overlap with this section. `docs/MOTHERPLANT_CHAMBER_PLAN.md` is a
> minimal slice (second chamber, pH correction only, shared HAL and SafetyGate). The runtime presets
> (Anzucht / Mutter / Haupt, `plan/03-presets.md`) switch one controller between systems instead of
> running two. Decide which of the items below are still wanted once the presets exist. Pin numbers
> and the I2C remarks below predate the current pinout (the pH circuit is on UART now).

**Architecture**: Duplicate FSM instances
- 2x PlantOSController (controller_chamber1, controller_chamber2)
- 2x ESPHomeHAL (hal_chamber1, hal_chamber2)
- 2x ActuatorSafetyGate (different safety rules according to tank volume)

### P3-1: Architecture Design
**Status**: Open
**Priority**: P5
**Effort**: 8-12 hours

**Tasks**:
- [ ] Design dual-controller architecture diagram
- [ ] GPIO pin allocation plan (14 actuators + 4 level sensors + sensors)
- [ ] Address I2C conflicts (two pH sensors at 0x61)
  - Option A: UART for both (different TX/RX pins)
  - Option B: I2C multiplexer (TCA9548A)
- [ ] Power supply plan (relay board capacity)
- [ ] Document in ARCHITECTURE_MULTI_CHAMBER.md

### P3-2: Second Chamber Hardware Configuration
**Status**: Open
**Priority**: P5
**Effort**: 6-10 hours

**Hardware Required**:
- 7 actuators for chamber 2 (GPIO21-27 or use GPIO expander MCP23017)
- 2 water level sensors (GPIO28-29)
- EZO pH sensor (UART on GPIO22/23 or I2C via multiplexer)
- DS18B20 temperature sensor (1-Wire multi-drop, different address)
- Light sensor (I2C ADC like ADS1115 at 0x48)

**GPIO Check**: ESP32-C6 has ~30 pins, Phase 3 needs 24 - feasible but tight

### P3-3: HAL Duplication
**Status**: Open
**Priority**: P5
**Effort**: 8-12 hours

**Tasks**:
- [ ] Create hal_chamber1 instance (rename current)
- [ ] Create hal_chamber2 instance (new hardware)
- [ ] Update dependency injection
- [ ] Test independently
- [ ] Verify no shared state

### P3-4: Controller Duplication
**Status**: Open
**Priority**: P5
**Effort**: 6-10 hours

**Tasks**:
- [ ] Create unified_controller_chamber1 (rename current)
- [ ] Create unified_controller_chamber2 (new instance)
- [ ] Each gets own HAL via dependency injection
- [ ] Both share ActuatorSafetyGate
- [ ] Test independent + simultaneous operation

### P3-5: Calendar Manager Enhancement
**Status**: Open
**Priority**: P5
**Effort**: 4-6 hours

**Features**:
- Chamber-specific schedules (120-day flowering vs custom vegetative)
- Web UI schedule selector
- Independent day counters per chamber

### P3-6: Web UI Enhancement
**Status**: Open
**Priority**: P5
**Effort**: 8-12 hours

**Features**:
- Chamber selector (dropdown or tabs)
- Duplicate all control buttons for each chamber
- Side-by-side status view
- Per-chamber state display

### P3-7: Multi-Chamber Testing
**Status**: Open
**Priority**: P5
**Effort**: 10-16 hours

**Test Cases**:
- [ ] Independent operation (chamber 1 runs while chamber 2 idle)
- [ ] Simultaneous pH correction (both inject acid)
- [ ] Simultaneous feeding (both dose nutrients)
- [ ] Error isolation (chamber 1 error doesn't stop chamber 2)
- [ ] SafetyGate enforces limits on both chambers
- [ ] Power loss recovery (both chambers resume)
- [ ] 7-day dual-chamber unattended operation

### Success Criteria

- [ ] Two chambers operate independently
- [ ] Simultaneous operations (both dosing)
- [ ] Web UI controls both chambers
- [ ] Independent schedules (120-day main, custom mother)
- [ ] No GPIO/sensor conflicts
- [ ] 7-day dual-chamber operation

**Recommendation**: Only pursue after 30+ days of stable single-chamber MVP operation

### Risks (multi-chamber only)

**R4: GPIO Pin Shortage**
- ESP32-C6 has 30 pins, Phase 3 needs 36
- Mitigation: I2C GPIO expander (MCP23017) or shift registers (74HC595)

**R5: I2C Address Conflict**
- Two EZO pH sensors both default to 0x61
- Mitigation: UART for one/both or I2C multiplexer (TCA9548A)

---

## Check: still relevant?

These were open or unticked in the old list. The feature behind each one exists in the code, or the
device has been running in production since (168 h uptime on 2026-10-04), but the item itself
cannot be confirmed from the code. Tick off or delete.

**From MVP-2 (Water Level Sensor Integration)**
- [ ] Test empty operation with low level abort (waiting on hardware)

**From MVP-3 (pH Dosing Calibration)**. The placeholder formula is gone; the dose now comes from the
self-adjusting K-factor.
- [ ] Validate accuracy (within ±0.1 pH of target)

**From MVP-4 (Error Handling Enhancement)**. Retry logic, sensor failure handling and the emergency
stop are in the code (`enhanced_error_handling: true`).
- [ ] Error counters in CentralStatusLogger
- [ ] More informative error logging (e.g. currently only PH_CORRECTION is logged, without outcome (e.g. Error - pH too low, needs manual intervention))
- [ ] Better clearing of PSM Alerts, once action (e.g. pH correction) is completed

**From MVP-5 (End-to-End Testing)**
- [ ] Automatic pH correction trigger
- [ ] Feeding sequence (all 3 pumps run sequentially)
- [ ] Water empty (abort on low level sensor)
- [ ] Power loss recovery (PSM validation)
- [ ] 24-hour unattended operation
- [ ] 48-hour pH stability (stays within 5.5-6.5)

**From P2-3 (Light Control Integration)**. The light is switched on a schedule through the Shelly.
- [ ] Sunrise/sunset simulation (gradual PWM ramping)

**From the Phase 2 success criteria**
- [ ] CSV logs created daily (belongs to P2-5 above)

**From the risk list**
- R1: pH Dosing Calibration Variability. Real-world conditions may cause formula to vary.
  Mitigation: Multiple data points, safety margins (start with 50% dose), adaptive dosing
- R6: Long-Term Reliability. System may fail after days/weeks.
  Mitigation: 7-day test before production, daily auto-restart at 3:00 AM
  (a daily auto-restart is not in the YAML)

**Ideas from docs that were deleted or archived on 2026-11-14** (not from the old TODO list)
- [ ] Raise the pump PWM frequency from 1000 Hz to 25 kHz against audible MOSFET squeal
      (the rest of the PWM slider plan is implemented)
- [ ] Vacation mode: reduced doses, more retries, repeated alerts, daily report (`docs/archive/Steuerungslogik.md` §2.5)
- [ ] Telegram alert backend (`alert_service` has only the log backend)
- [ ] Manual confirmation of grow-phase changes (`docs/archive/Steuerungslogik.md` §7)
- [ ] Minimum interval between acid injections enforced in the SafetyGate (`docs/archive/pH_Regellogik.md` §7)

---

## Removed on 2026-11-14 because the code shows them done

- MVP-1 GPIO Actuator Configuration (five PWM outputs on GPIO1/4/5/6/7, three actuators on the Shelly)
- MVP-2 Water Level Sensor Integration (three XKC-Y23-V sensors, abort logic in fill and empty)
- MVP-3 pH Dosing Calibration (adaptive K-factor instead of the placeholder formula)
- MVP-4 retry logic, sensor failure handling, emergency stop
- P2-1 Complete 120-Day Schedule
- P2-2 Automated Triggers
- P2-3 Light Control Integration (schedule; without sunrise/sunset ramping)
- P2-4 Air Pump Schedule Control
- P2-6 Permanent Logging (MQTT to the Hetzner server, InfluxDB, Grafana)
- P2-7 TDS/EC Sensor Integration (KS0429 analog probe instead of the EZO-EC)
- Task #2 Real pH Sensor, Task #5 Temperature Monitoring
- Risks R2 (power supply) and R3 (level sensor voltage): solved in the hardware that is running
- The status header (85 %, "Last Updated 2025-12-23"), the timeline summary and the reference to
  a plan file outside the repo

---

## From the Nov 2026 overhaul

