# PlantOS TODO List

**Project Status**: 85% Complete - Architecture & Sensors Working
**Current Phase**: MVP Finalization
**Last Updated**: 2025-12-23

This document tracks tasks organized into three phases:
1. **MVP** - Minimum Viable Product (autonomous pH control + feeding)
2. **More Features** - Enhanced monitoring and automation
3. **More Chambers** - Multi-chamber support (main + mother plant)

> **Reference**: See `/home/cody/.claude/plans/snazzy-yawning-rocket.md` for detailed implementation plan

---

## Project Status Summary

### ✅ Completed (85%)

**Architecture & Core Systems**:
- ✅ 3-layer HAL (Controller → SafetyGate → HAL)
- ✅ Unified FSM with 12 states + LED behavior system
- ✅ ActuatorSafetyGate (debouncing, duration limits, soft-start/stop)
- ✅ Persistent State Manager (NVS crash recovery)
- ✅ Watchdog timer + Central Status Logger
- ✅ Web interface with manual control buttons

**Sensors**:
- ✅ EZO pH UART sensor (working, commits 8009973, 7d9d358)
- ✅ DS18B20 temperature sensor (GPIO10)
- ✅ KY-046 light sensor (GPIO0)
- ✅ Sensor filtering with outlier rejection
- ✅ Temperature compensation integrated

**Control Logic**:
- ✅ pH correction sequence (MEASURING → CALCULATING → INJECTING → MIXING)
- ✅ pH calibration (3-point: 4.00, 7.00, 10.01)
- ✅ Feeding sequence (A→B→C sequential pumps)
- ✅ Water management states (FILLING, EMPTYING)
- ✅ Maintenance mode with NVS persistence

### ❌ Critical Blockers (15%)

1. **GPIO Actuator Wiring** - HAL has stub implementations, no real GPIO pins assigned
2. **Water Level Sensors** - Not configured (2x XKC-Y23-V required for safe operation)
3. **pH Dosing Calibration** - Placeholder formula needs real-world calibration

---

## Phase 1: MVP (Minimum Viable Product)

**Goal**: Autonomous hydroponic system maintaining pH 5.5-6.5 and feeding on schedule
**Timeline**: 19-30 hours (1-2 weeks)
**Status**: 85% complete

### MVP-1: GPIO Actuator Configuration ✅ COMPLETED
**Status**: ✅ COMPLETED (2025-12-26)
**Priority**: P0 (blocks all hardware testing)
**Effort**: 4-6 hours (actual: ~6 hours)

**Required Actuators** (7 total) - ACTUAL GPIO PINS USED:
1. AcidPump (pH down) - GPIO19 ✓
2. NutrientPumpA (grow) - GPIO20 ✓
3. NutrientPumpB (micro) - GPIO21 ✓
4. NutrientPumpC (bloom) - GPIO22 ✓
5. WaterValve (fresh water) - GPIO18 ✓
6. WastewaterPump (drainage) - GPIO23 ✓
7. AirPump (mixing) - GPIO11 ✓

**Tasks**:
- [x] Add 7 GPIO output components to plantOS.yaml (GPIO11, 18-23)
- [x] Add 7 switch components wrapping outputs (template switches via safety gate)
- [x] Update HAL implementation (hal.cpp) to control actual GPIOs via switches
- [x] Add dependency injection in hal.h and __init__.py for all switches
- [x] Test buttons (3s auto-off) added for manual testing
- [x] Manual toggle switches integrated with safety gate auto-revert
- [x] Safety gate enable/disable switch added
- [ ] Test each actuator individually via web UI buttons (pending hardware)
- [ ] Verify SafetyGate duration limits work (pending hardware)

**Files Modified**:
- `plantOS.yaml`: Added 2 outputs (wastewater, air), 2 switches + dependency injection (~100 lines)
- `components/plantos_hal/hal.cpp`: Replaced stubs with actual switch control (~60 lines)
- `components/plantos_hal/hal.h`: Added 2 switch members + setters (~15 lines)
- `components/plantos_hal/__init__.py`: Added 2 switch injections (~40 lines)

**BONUS Improvements Beyond Original Plan**:
- ✅ All switches route through ActuatorSafetyGate (better safety)
- ✅ Test buttons with 3s auto-off (easy testing)
- ✅ Manual toggles with auto-revert on rejection
- ✅ Safety gate master enable/disable switch
- ✅ Fixed debouncing interference between buttons/switches
- ✅ Optimistic switch mode prevents cross-control issues

**Note**: Original TODO specified GPIO11-17, but actual implementation uses GPIO18-23 + GPIO11 due to ESP32-C6 pin availability and existing working configuration. This is functionally equivalent.

**Risk**: ESP32 can't source enough current for pumps - use external relay board with 12V/24V supply

---

### MVP-2: Water Level Sensor Integration ⚠️ CRITICAL
**Status**: Open
**Priority**: P0 (required for safe operation)
**Effort**: 3-4 hours
**Depends on**: MVP-1

**Hardware**: 2x XKC-Y23-V 5V capacitive level sensors

**Tasks**:
- ✅ Configure 2x XKC-Y23-V as binary sensors (GPIO18=high, GPIO19=low)
- ✅ Add water level checking to WATER_FILLING handler (abort on HIGH)
- ✅ Add water level checking to WATER_EMPTYING handler (abort on LOW)
- ✅ Add level status to CentralStatusLogger
- ✅ Test fill operation with high level abort
- [ ] Test empty operation with low level abort (waiting on hardware)

**Files to modify**:
- `plantOS.yaml`: Add 2 binary sensor components (~15-20 lines)
- `components/plantos_controller/controller.cpp`: Update WATER_FILLING/EMPTYING handlers (~20-30 lines)
- `components/plantos_hal/hal.h` + `hal.cpp`: Add level sensor methods (~15-20 lines)

**Risk**: XKC-Y23-V is 5V, ESP32-C6 is 3.3V - voltage divider required (2kΩ + 3.3kΩ)

---

### MVP-3: pH Dosing Calibration
**Status**: Open
**Priority**: P1 (affects accuracy)
**Effort**: 6-10 hours
**Depends on**: MVP-1

**Current Formula**: `duration_ms = ph_diff * 10.0f * 1000.0f` (PLACEHOLDER)

**Calibration Protocol**:
1. Measure reservoir volume (liters)
2. Measure acid concentration (% or pH of solution)
3. Run test series (5+ data points):
   - pH 7.0 → 6.5
   - pH 7.0 → 6.0
   - pH 6.5 → 6.0
   - pH 6.5 → 5.5
   - pH 6.0 → 5.5
4. Record: pump duration (seconds) → pH change after mixing
5. Create dosing lookup table or regression formula
6. Update `calculate_acid_duration()` in controller.cpp
7. Validate accuracy (within ±0.1 pH of target)

**Files to modify**:
- `components/plantos_controller/controller.cpp`: Update calculate_acid_duration() (~15-25 lines)
- `CALIBRATION.md` (create new): Document experiment and formula (~150-250 lines)

**Safety**: Start with 50% of calculated dose, measure result, adjust if needed

---

### MVP-4: Error Handling Enhancement (OPTIONAL)
**Status**: Open
**Priority**: P2 (recommended for production, optional for MVP)
**Effort**: 8-12 hours

**Features**:
- Retry logic for SafetyGate rejections (max 3 attempts)
- Sensor failure handling (use last valid reading, alert after 5 errors)
- Emergency stop function (all actuators OFF + critical alert)
- Error counters in CentralStatusLogger
- More informative error logging (e.g. currently only PH_CORRECTION is logged, without outcome (e.g. Error - pH too low, needs manual intervention))
- Better clearing of PSM Alerts, once action (e.g. pH correction) is completed

**Files to modify**:
- `components/plantos_controller/controller.h`: Add error tracking
- `components/plantos_controller/controller.cpp`: Implement retry logic

**From original TODO**: Task #6 (Error Handling and Recovery)

---

### MVP-5: End-to-End Testing ⚠️ CRITICAL
**Status**: Open
**Priority**: P0 (validation before production)
**Effort**: 6-10 hours
**Depends on**: MVP-1, MVP-2, MVP-3

**Test Cases**:
- ✅ Manual pH correction (web UI button)
- [ ] Automatic pH correction trigger
- [ ] Feeding sequence (all 3 pumps run sequentially)
- ✅ Water fill (abort on high level sensor)
- [ ] Water empty (abort on low level sensor)
- ✅ Temperature compensation (verify temp sent to pH sensor)
- [ ] Power loss recovery (PSM validation)
- ✅ SafetyGate duration limits (try to exceed, verify rejection)
- ✅ pH calibration (3-point: 4.00, 7.00, 10.01)
- [ ] 24-hour unattended operation
- [ ] 48-hour pH stability (stays within 5.5-6.5)

**From original TODO**: Task #14 (End-to-End Testing)

---

### MVP Success Criteria

- [ ] All 7 actuators respond to web UI
- [ ] pH correction runs end-to-end automatically
- [ ] Feeding runs on schedule with all 3 nutrient pumps
- [ ] Water fill/empty aborts safely on level sensors
- [ ] Temperature compensation works
- [ ] PSM recovers after power loss
- [ ] SafetyGate enforces duration limits
- [ ] 24-hour unattended operation successful
- [ ] pH stays within 5.5-6.5 for 48 hours

**MVP Timeline**: 1-2 weeks of focused work
**MVP Deliverable**: Autonomous hydroponic system maintaining pH and feeding on schedule

---

## Phase 2: More Features

**Goal**: Enhanced monitoring, automation, and data management
**Timeline**: 40-68 hours
**Status**: Not started (waiting for MVP completion)

### P2-1: Complete 120-Day Schedule
**Status**: Open
**Priority**: P3
**Effort**: 4-8 hours

Create full 120-day grow cycle JSON schedule with:
- Daily pH targets (min/max)
- Nutrient dosing durations (A/B/C) per day
- Validate JSON structure

**Files to modify**:
- `plantOS.yaml`: Update calendar_manager.schedule_json
- Create schedule JSON file (external)

**From original TODO**: Task #1

---

### P2-2: Automated Triggers
**Status**: Open
**Priority**: P3
**Effort**: 8-12 hours

**Triggers to Implement**:
- Time-based: pH check every 2h, feeding at 8:00 AM
- Sensor-based: pH > target_max + 0.2 triggers correction
- Respect safe_mode: Disable auto-triggers when safe_mode enabled

**Files to modify**:
- `components/plantos_controller/controller.h`: Add trigger config
- `components/plantos_controller/controller.cpp`: Implement trigger logic
- `plantOS.yaml`: Add trigger configuration

**From original TODO**: Task #7

---

### P2-3: Light Control Integration
**Status**: Open
**Priority**: P3
**Effort**: 4-6 hours

**Features**:
- Add light control actuator (GPIO20 or next available)
- Sunrise/sunset simulation (gradual PWM ramping)
- Schedule integration (daily on/off times)

**Files to modify**:
- `plantOS.yaml`: Add light output/switch
- `components/plantos_hal/hal.h` + `hal.cpp`: Add setLight() method
- `components/plantos_controller/controller.cpp`: Add LIGHT_CONTROL state

---

### P2-4: Air Pump Schedule Control
**Status**: Open
**Priority**: P3
**Effort**: 2-4 hours

**Features**:
- Periodic aeration (e.g., 15 min every 2h)
- Schedule integration
- Independent of pH_MIXING state

**Files to modify**:
- `components/plantos_controller/controller.cpp`: Add AIR_SCHEDULE state or timer

---

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

**From original TODO**: Task #11 (partial)

---

### P2-6: Permanent Logging (Cloud Upload)
**Status**: Open
**Priority**: P4
**Effort**: 8-16 hours

**Features**:
- Cloud storage (MQTT, HTTP POST, or InfluxDB)
- Retry logic (queue if network down)
- Dashboard integration (Grafana, Home Assistant)

**Files to modify**:
- `plantOS.yaml`: Add MQTT or HTTP client config
- Create cloud logging component

**From original TODO**: Task #11 (partial)

---

### P2-7: TDS/EC Sensor Integration
**Status**: Open
**Priority**: P4
**Effort**: 6-10 hours

**Hardware**: Atlas Scientific EZO-EC sensor

**Features**:
- Monitor nutrient concentration
- Adjust feeding dosing based on current TDS
- Add TDS target ranges to calendar schedule

**Files to modify**:
- Create new component: `ezo_ec` (similar to ezo_ph_uart)
- `components/plantos_controller/controller.cpp`: Integrate TDS into feeding logic

**From original TODO**: Task #10

---

### Phase 2 Success Criteria

- [ ] 120-day schedule loaded and used
- [ ] Automated pH correction (every 2h)
- [ ] Automated feeding (daily at 8:00 AM)
- [ ] Light control on schedule (sunrise/sunset simulation)
- [ ] Air pump schedule (15 min every 2h)
- [ ] CSV logs created daily
- [ ] 7-day unattended operation

**Recommended Sequence**:
1. P2-1 + P2-2 (Automated operation) - highest value
2. P2-3 + P2-4 (Light/air control)
3. P2-5 (Local logging)
4. P2-6 + P2-7 (Advanced features)

---

## Phase 3: More Chambers (Multi-Chamber Support)

**Goal**: Control two independent growth chambers (main + mother plant)
**Timeline**: 50-78 hours
**Status**: Not started (waiting for MVP + Phase 2 stability)

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

---

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

---

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

---

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

---

### P3-5: Calendar Manager Enhancement
**Status**: Open
**Priority**: P5
**Effort**: 4-6 hours

**Features**:
- Chamber-specific schedules (120-day flowering vs custom vegetative)
- Web UI schedule selector
- Independent day counters per chamber

---

### P3-6: Web UI Enhancement
**Status**: Open
**Priority**: P5
**Effort**: 8-12 hours

**Features**:
- Chamber selector (dropdown or tabs)
- Duplicate all control buttons for each chamber
- Side-by-side status view
- Per-chamber state display

---

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

---

### Phase 3 Success Criteria

- [ ] Two chambers operate independently
- [ ] Simultaneous operations (both dosing)
- [ ] Web UI controls both chambers
- [ ] Independent schedules (120-day main, custom mother)
- [ ] No GPIO/sensor conflicts
- [ ] 7-day dual-chamber operation

**Recommendation**: Only pursue after 30+ days of stable single-chamber MVP operation

---

## Completed Tasks ✅

### Task #2: Integrate Real pH Sensor (EZO pH via UART)
**Status**: ✅ COMPLETED (2025-12-04)
**Commits**: 8009973, 7d9d358 (EZO pH UART fixes, readings working)

Production-ready implementation complete with:
- UART communication at 115200 baud
- 3-point calibration support
- Temperature compensation
- Stability detection
- Error handling

---

### Task #5: Add Temperature Monitoring
**Status**: ✅ COMPLETED (2025-12-23)
**Implementation**: DS18B20 on GPIO10

Temperature compensation fully integrated:
- Sensor configured in plantOS.yaml (lines 368-375)
- HAL integration complete (hal.cpp)
- Temperature sent to pH sensor for ATC (Automatic Temperature Compensation)
- Displayed in CentralStatusLogger

**Note**: pH readings are temperature-dependent (±0.003 pH per °C), so this is critical for accuracy

---

## Deferred Tasks (Future Enhancements)

### Task #12: Mobile-Friendly Dashboard
**Priority**: Low
**Effort**: 16-24 hours
**Reason**: Web UI sufficient for MVP, nice-to-have for later

---

### Task #13: Advanced pH Control (PID)
**Priority**: Low
**Effort**: 16-24 hours
**Reason**: Simple threshold control works well, PID adds complexity

---

### Task #15: User Manual
**Priority**: Low
**Effort**: 8-12 hours
**Reason**: Post-production documentation, after system is stable

---

### Task #16: API Documentation
**Priority**: Low
**Effort**: 4-8 hours
**Reason**: Post-production documentation, after API is finalized

---

## Risk Assessment

### HIGH Risk ⚠️

**R1: pH Dosing Calibration Variability**
- Real-world conditions may cause formula to vary
- Mitigation: Multiple data points, safety margins (start with 50% dose), adaptive dosing

**R2: Power Supply Limitation**
- ESP32 can't source enough current for 7+ relays
- Mitigation: External relay board / MOSFETs with 12V/5V supply, ESP32 drives coils only

### MEDIUM Risk ⚠

**R3: Water Level Sensor Compatibility**
- XKC-Y23-V is 5V, ESP32-C6 is 3.3V logic
- Mitigation: Use 3.3V, which is in input range.

**R4: GPIO Pin Shortage (Phase 3 only)**
- ESP32-C6 has 30 pins, Phase 3 needs 36
- Mitigation: I2C GPIO expander (MCP23017) or shift registers (74HC595)

**R5: I2C Address Conflict (Phase 3 only)**
- Two EZO pH sensors both default to 0x61
- Mitigation: UART for one/both or I2C multiplexer (TCA9548A)

### LOW Risk ℹ️

**R6: Long-Term Reliability**
- System may fail after days/weeks
- Mitigation: 7-day test before production, daily auto-restart at 3:00 AM

---

## Summary

**Total Open Tasks**: 19 (5 MVP + 7 Phase 2 + 7 Phase 3)
**Completed Tasks**: 2 (pH sensor, temperature sensor)

**Critical Path to MVP**:
```
MVP-1 (GPIO Actuators, 4-6h)
  ↓
MVP-2 (Water Level, 3-4h)
  ↓
MVP-3 (Dosing Calibration, 6-10h)
  ↓
MVP-5 (Testing, 6-10h)

Total: 19-30 hours
```

**Recommended Next Steps**:
1. Complete MVP-1 (GPIO actuators) - CRITICAL BLOCKER
2. Complete MVP-2 (Water level sensors) - CRITICAL for safety
3. Complete MVP-3 (Dosing calibration) - Affects accuracy
4. Complete MVP-5 (End-to-end testing) - Validation
5. Phase 2: Pick features based on priority (automation, logging, environmental control)
6. Phase 3: Only after 30+ days of stable single-chamber operation

**Expected Timeline**:
- MVP: 1-2 weeks
- Phase 2: 2-3 weeks (pick subset of features)
- Phase 3: 2-3 weeks (only if multi-chamber needed)

---

**Last Updated**: 2025-12-23
**Version**: 2.0 (Reorganized into 3 phases)
**Reference Plan**: `/home/cody/.claude/plans/snazzy-yawning-rocket.md`
