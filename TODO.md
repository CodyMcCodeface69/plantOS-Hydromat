# PlantOS TODO List

This document tracks the open tasks and next steps for completing the PlantOS system after implementing the core application logic in v0.5.0.

## High Priority - Production Readiness

### 1. Complete 120-Day Schedule Configuration
**Status**: Open
**Priority**: High
**Description**: The current configuration only includes a 3-day sample schedule. Need to create and configure the full 120-day grow cycle schedule.

**Tasks**:
- [ ] Create complete 120-day schedule with daily pH targets (min/max)
- [ ] Define nutrient dosing durations (A/B/C) for each day
- [ ] Validate JSON structure for all 120 days
- [ ] Update `calendar_manager.schedule_json` in plantOS.yaml
- [ ] Test schedule loading and validation on boot

**Files to modify**:
- `plantOS.yaml` (lines 684-689)

**Estimated effort**: 4-8 hours (research + data entry + validation)

---

### 2. Integrate Real pH Sensor (EZOPH)
**Status**: Open
**Priority**: High
**Description**: Currently using dummy sensor for development. Need to integrate the actual EZOPH pH sensor for production use.

**Tasks**:
- [ ] Create EZOPH sensor component (or use existing library)
- [ ] Implement I²C communication protocol for EZOPH
- [ ] Add pH sensor calibration logic to `calibrate_ph()` method
- [ ] Replace `ph_sensor: sensor_dummy_id` with actual sensor ID
- [ ] Test pH reading accuracy and stability
- [ ] Implement calibration state machine in PlantOSLogic
- [ ] Add calibration status to CentralStatusLogger

**Files to create/modify**:
- `components/ezoph_sensor/` (new component)
- `plantOS.yaml` (line 769)
- `components/plantos_logic/PlantOSLogic.cpp` (handle_ph_calibrating method)

**Dependencies**:
- EZOPH sensor hardware
- I²C wiring (SDA/SCL)
- Calibration solutions (pH 4.0, 7.0, 10.0)

**Estimated effort**: 8-16 hours

---

### 3. Configure Physical Actuators (Pumps & Valves)
**Status**: Open
**Priority**: High
**Description**: Connect and configure all physical actuators required for pH control, feeding, and water management.

**Hardware Required**:
- Acid pump (pH down)
- Nutrient pumps (A, B, C)
- Air pump (mixing)
- Freshwater valve
- Wastewater pump

**Tasks**:
- [ ] Wire all pumps/valves to GPIO pins or relay board
- [ ] Create ESPHome switch components for each actuator
- [ ] Configure maximum duration limits for each actuator via `setMaxDuration()`
- [ ] Test individual actuator control via Web UI
- [ ] Verify safety gate rejection of excessive durations
- [ ] Add actuator status to CentralStatusLogger

**Files to modify**:
- `plantOS.yaml` (add switch components for each actuator)
- Boot automation lambda (configure safety gate max durations)

**Example configuration needed**:
```yaml
switch:
  - platform: gpio
    pin: GPIO10
    id: acid_pump_switch
    name: "Acid Pump"

  # ... similar for all other actuators

esphome:
  on_boot:
    priority: -100
    then:
      - lambda: |-
          // Configure actuator safety limits
          id(actuator_safety)->setMaxDuration("AcidPump", 30);  // 30 seconds max
          id(actuator_safety)->setMaxDuration("NutrientPumpA", 60);  // 60 seconds max
          // ... etc
```

**Estimated effort**: 4-8 hours (hardware + configuration + testing)

---

### 4. Calibrate Acid Dosing Formula
**Status**: Open
**Priority**: Medium
**Description**: Replace the placeholder linear dosing formula with a calibrated curve based on actual system behavior.

**Current Implementation**:
```cpp
// Placeholder: 1 second per 0.1 pH unit
uint32_t duration_ms = static_cast<uint32_t>(ph_diff * 10.0f * 1000.0f);
```

**Tasks**:
- [ ] Measure reservoir volume
- [ ] Measure acid concentration
- [ ] Perform calibration test series:
  - Test different pH starting points (6.5, 6.0, 5.5)
  - Test different dosing durations (1s, 2s, 5s, 10s)
  - Measure resulting pH change after mixing
- [ ] Create dosing curve lookup table or formula
- [ ] Implement non-linear dosing calculation if needed
- [ ] Add safety bounds (min/max duration)
- [ ] Document calibration data and formula

**Files to modify**:
- `components/plantos_logic/PlantOSLogic.cpp` (calculate_acid_duration method)

**Estimated effort**: 4-8 hours (experimentation + data analysis + implementation)

---

### 5. Add Temperature Monitoring (CRITICAL FOR pH ACCURACY)
**Status**: Open
**Priority**: High
**Description**: Monitor reservoir temperature for pH compensation and optimal plant health. **CRITICAL**: pH readings are temperature-dependent and require compensation for accurate measurements.

**Why This Is Critical**:
- pH electrodes are temperature-sensitive (±0.003 pH per °C)
- Accurate pH control requires temperature compensation
- Most pH meters have built-in ATC (Automatic Temperature Compensation) that requires temperature input
- Without temperature compensation, pH readings can be off by 0.1-0.3 pH units

**Hardware Required**:
- Waterproof DS18B20 temperature sensor (common, inexpensive)
- Or temperature probe integrated with EZOPH sensor

**Tasks**:
- [ ] Add waterproof DS18B20 temperature sensor to reservoir
- [ ] Create or integrate temperature sensor component
- [ ] Pass temperature reading to pH sensor for ATC (Automatic Temperature Compensation)
- [ ] Display temperature in CentralStatusLogger
- [ ] Add temperature alerts (too hot/cold)
  - Optimal range: 18-25°C (65-77°F)
  - Critical alerts: <15°C or >30°C
- [ ] Implement temperature-compensated pH calculations in PlantOSLogic
- [ ] Store temperature alongside pH readings for logging

**Files to create/modify**:
- `plantOS.yaml` (add DS18B20 sensor configuration)
- `components/plantos_logic/PlantOSLogic.h` (add temperature sensor reference)
- `components/plantos_logic/PlantOSLogic.cpp` (use temperature for pH compensation)
- EZOPH sensor component (pass temperature for ATC)

**Temperature Compensation Formula** (if manual compensation needed):
```
pH_compensated = pH_measured - (α × (T_measured - T_reference))
where α ≈ 0.003 pH/°C, T_reference = 25°C
```

**Estimated effort**: 2-4 hours (critical path item)

**Dependencies**: Must be completed before pH sensor calibration (Task #2)

---

### 6. Implement Error Handling and Recovery (CRITICAL)
**Status**: Open
**Priority**: High
**Description**: Implement comprehensive error handling for all failure scenarios, especially when ActuatorSafetyGate rejects commands or routines fail to execute.

**Current Problem**:
- When ActuatorSafetyGate rejects a command (duration exceeded, debouncing), the routine may fail silently
- No automatic retry logic for transient failures
- No user notification of critical failures
- Incomplete sequences may leave system in inconsistent state

**Error Scenarios to Handle**:

1. **ActuatorSafetyGate Rejection**:
   - Duration exceeds configured maximum
   - Debouncing rejection (duplicate command)
   - Actuator already running beyond safe duration

2. **Sensor Failures**:
   - pH sensor returns NaN or invalid reading
   - Temperature sensor disconnected
   - Sensor reading timeout

3. **Sequence Failures**:
   - pH correction fails after max retries (5 attempts)
   - Feeding interrupted mid-sequence
   - Water fill/empty timeout (no level sensor feedback)

4. **System Failures**:
   - PSM recovery indicates interrupted critical operation
   - Memory allocation failures
   - I²C communication timeout

**Recovery Strategies to Implement**:

**A. Retry Logic** (for transient failures):
```cpp
enum class ErrorRecoveryAction {
    RETRY,           // Attempt operation again after delay
    SKIP,            // Skip current step, continue sequence
    ABORT,           // Stop sequence, return to IDLE
    NOTIFY_CONTINUE, // Log error but continue operation
    EMERGENCY_STOP   // Stop all actuators, raise critical alert
};
```

**B. Error Handling Configuration**:
```yaml
plantos_logic:
  error_handling:
    max_retry_attempts: 3
    retry_delay: 5s
    safety_gate_rejection_action: ABORT  # or RETRY, SKIP
    sensor_failure_action: NOTIFY_CONTINUE  # or ABORT
    sequence_failure_action: ABORT
```

**C. Implementation Tasks**:
- [ ] Define `ErrorRecoveryAction` enum and error types
- [ ] Add retry counter and delay tracking to each state
- [ ] Implement retry logic in critical states:
  - PH_INJECTING: Retry if SafetyGate rejects (up to 3 times)
  - FEEDING_INJECTING: Skip failed pump, continue with next
  - WATER_FILLING: Abort after 5 minutes (timeout)
- [ ] Add error state tracking to PSM for recovery on reboot
- [ ] Implement emergency stop function (all actuators OFF)
- [ ] Add error counters to CentralStatusLogger
- [ ] Create alert categories:
  - WARNING: Retry in progress
  - ERROR: Sequence aborted
  - CRITICAL: Emergency stop triggered
- [ ] Add Web UI error display and manual intervention buttons
- [ ] Log all errors with timestamp and recovery action taken

**D. Specific Error Handlers**:

**pH Injection Failure**:
```cpp
if (!this->safety_gate_->executeCommand(ACTUATOR_ACID_PUMP, true, dose_sec)) {
    ESP_LOGE(TAG, "Acid pump rejected by SafetyGate!");

    if (retry_count_ < MAX_RETRY_ATTEMPTS) {
        retry_count_++;
        ESP_LOGW(TAG, "Retrying in 5 seconds (attempt %d/%d)",
                 retry_count_, MAX_RETRY_ATTEMPTS);
        // Wait 5 seconds, then retry
        this->transition_to(LogicStatus::PH_RETRY_DELAY);
    } else {
        ESP_LOGE(TAG, "Max retries exceeded - aborting pH correction");
        this->status_logger_->updateAlertStatus("PH_INJECTION_FAILED",
                                                "Acid pump rejected after retries");
        this->psm_->clearEvent();
        this->emergency_stop();
        this->transition_to(LogicStatus::IDLE);
    }
    return;
}
```

**Sensor Invalid Reading**:
```cpp
if (std::isnan(ph) || ph < 0.0f || ph > 14.0f) {
    ESP_LOGW(TAG, "Invalid pH reading: %.2f", ph);

    if (sensor_error_count_ < MAX_SENSOR_ERRORS) {
        sensor_error_count_++;
        // Continue with last valid reading
        ESP_LOGW(TAG, "Using last valid pH: %.2f", this->ph_current_);
    } else {
        ESP_LOGE(TAG, "Too many sensor errors - aborting");
        this->status_logger_->updateAlertStatus("SENSOR_FAILURE",
                                                "pH sensor invalid readings");
        this->psm_->clearEvent();
        this->transition_to(LogicStatus::IDLE);
    }
    return;
}
```

**Feeding Pump Failure**:
```cpp
if (!this->safety_gate_->executeCommand(pump_name, true, duration_sec)) {
    ESP_LOGW(TAG, "%s rejected - skipping to next nutrient", pump_name);

    this->status_logger_->updateAlertStatus("PUMP_SKIPPED",
        String(pump_name) + " rejected by SafetyGate");

    // Skip this pump, continue with next
    this->state_counter_++;
    this->state_start_time_ = millis();
    return;
}
```

**E. Emergency Stop Function**:
```cpp
void PlantOSLogic::emergency_stop() {
    ESP_LOGE(TAG, "=== EMERGENCY STOP TRIGGERED ===");

    // Turn off all actuators immediately
    this->turn_all_pumps_off();

    // Clear any pending PSM events
    if (this->psm_) {
        this->psm_->clearEvent();
    }

    // Raise critical alert
    if (this->status_logger_) {
        this->status_logger_->updateAlertStatus("EMERGENCY_STOP",
                                                "System emergency stop triggered");
    }

    // Log to PSM for recovery tracking
    if (this->psm_) {
        this->psm_->logEvent("EMERGENCY_STOP", 0);
    }
}
```

**F. Testing Requirements**:
- [ ] Test SafetyGate rejection scenarios
- [ ] Test sensor disconnect/invalid readings
- [ ] Test max retry exhaustion
- [ ] Test emergency stop functionality
- [ ] Test PSM recovery after error-induced stop
- [ ] Verify all error alerts appear in Web UI
- [ ] Verify error counters in status logger

**Files to modify**:
- `components/plantos_logic/PlantOSLogic.h` (add error handling infrastructure)
- `components/plantos_logic/PlantOSLogic.cpp` (implement retry/recovery logic)
- `components/plantos_logic/__init__.py` (add error config schema)
- `plantOS.yaml` (error handling configuration)

**Estimated effort**: 12-20 hours (critical path item)

**Dependencies**: Should be completed before production deployment

---

## Medium Priority - Enhanced Functionality

### 7. Implement Automated Triggers
**Status**: Open
**Priority**: Medium
**Description**: Add time-based and sensor-based triggers for automatic routine execution.

**Suggested Triggers**:
- **pH Correction**: Trigger every 12 hours or when pH > target_max + 0.2
- **Feeding**: Trigger daily at specific time (e.g., 8:00 AM)
- **Water Top-Off**: Trigger when water level sensor indicates low level
- **Water Change**: Trigger weekly or bi-weekly

**Tasks**:
- [ ] Add water level sensor component
- [ ] Implement time-based triggers using ESPHome's `time` component
- [ ] Add sensor threshold triggers in PlantOSLogic::loop()
- [ ] Respect `safe_mode` flag (disable auto-triggers when enabled)
- [ ] Add trigger configuration to YAML (enable/disable each trigger)
- [ ] Test automated trigger execution
- [ ] Document trigger behavior and configuration

**Files to modify**:
- `components/plantos_logic/PlantOSLogic.h` (add trigger configuration)
- `components/plantos_logic/PlantOSLogic.cpp` (implement trigger logic)
- `components/plantos_logic/__init__.py` (add config schema)
- `plantOS.yaml` (trigger configuration)

**Estimated effort**: 8-12 hours

---

### 8. Configure Water Management Durations
**Status**: Open
**Priority**: Medium
**Description**: Replace hardcoded fill/empty durations with configurable parameters.

**Current Implementation**:
```cpp
static constexpr uint32_t FILL_DURATION_MS = 30000;  // Hardcoded 30s
static constexpr uint32_t EMPTY_DURATION_MS = 30000; // Hardcoded 30s
```

**Tasks**:
- [ ] Add configuration parameters to PlantOSLogic component
- [ ] Make durations settable via YAML
- [ ] Add runtime adjustment capability (buttons or API)
- [ ] Consider water level sensor integration for automatic shutoff
- [ ] Test various durations with actual reservoir

**Files to modify**:
- `components/plantos_logic/PlantOSLogic.h` (add config members)
- `components/plantos_logic/PlantOSLogic.cpp` (use config values)
- `components/plantos_logic/__init__.py` (add config schema)
- `plantOS.yaml` (add configuration)

**Estimated effort**: 2-4 hours

---

### 9. Add Water Level Monitoring
**Status**: Open
**Priority**: Medium
**Description**: Integrate water level sensors for tank monitoring and automatic control.

**Hardware Options**:
- Float switches (simple on/off)
- Ultrasonic sensors (continuous level)
- Capacitive sensors (no moving parts)

**Tasks**:
- [ ] Select and acquire water level sensor(s)
- [ ] Create or integrate sensor component
- [ ] Add water level to CentralStatusLogger
- [ ] Implement automatic water management triggers
- [ ] Add overfill/underfill alerts
- [ ] Integrate with WATER_FILLING/EMPTYING states for automatic shutoff

**Files to create/modify**:
- Sensor component files (if creating custom component)
- `plantOS.yaml` (sensor configuration)
- `components/plantos_logic/PlantOSLogic.cpp` (water level integration)

**Estimated effort**: 4-8 hours

---

## Low Priority - Nice-to-Have Features

### 10. Add TDS/EC Sensor for Nutrient Monitoring
**Status**: Open
**Priority**: Low
**Description**: Monitor nutrient concentration (Total Dissolved Solids or Electrical Conductivity) for better feeding control.

**Tasks**:
- [ ] Integrate TDS/EC sensor
- [ ] Add readings to CentralStatusLogger
- [ ] Adjust feeding durations based on current TDS
- [ ] Add TDS target ranges to CalendarManager schedule
- [ ] Implement TDS-based feeding logic

**Estimated effort**: 8-12 hours

---

### 11. Implement Data Logging to SD Card or Cloud
**Status**: Open
**Priority**: Low
**Description**: Log all sensor readings and routine executions for historical analysis.

**Tasks**:
- [ ] Add SD card component (if using local storage)
- [ ] Create CSV logging format
- [ ] Log pH readings, dosing events, feeding events
- [ ] Implement log rotation (daily/weekly files)
- [ ] Optional: Upload logs to cloud storage (MQTT, HTTP POST)

**Estimated effort**: 8-16 hours

---

### 12. Create Mobile-Friendly Dashboard
**Status**: Open
**Priority**: Low
**Description**: Enhance the web UI with a more intuitive, mobile-friendly interface.

**Tasks**:
- [ ] Design dashboard layout (charts, gauges, status cards)
- [ ] Add real-time pH chart (last 24 hours)
- [ ] Add nutrient dosing history
- [ ] Add routine execution timeline
- [ ] Improve button layouts for mobile touch
- [ ] Add dark/light theme toggle

**Estimated effort**: 16-24 hours

---

### 13. Implement Advanced pH Control Algorithm
**Status**: Open
**Priority**: Low
**Description**: Replace simple threshold-based control with PID or adaptive control.

**Tasks**:
- [ ] Research PID tuning for pH control
- [ ] Implement PID controller
- [ ] Auto-tune PID parameters based on system response
- [ ] Compare performance vs. simple threshold control
- [ ] Add PID configuration to YAML

**Estimated effort**: 16-24 hours

---

## Testing & Validation

### 14. End-to-End Testing Plan
**Status**: Open
**Priority**: High
**Description**: Create comprehensive test plan for all routines before production deployment.

**Test Cases**:
- [ ] pH correction with various starting pH values (5.5, 6.0, 6.5, 7.0)
- [ ] Feeding sequence with all 3 nutrients
- [ ] Tank fill/empty operations
- [ ] Safe mode verification (manual only, no auto-triggers)
- [ ] Power loss recovery (PSM validation)
- [ ] Safety gate duration limit enforcement
- [ ] Critical pH alert triggering (<5.0, >7.5)
- [ ] Max retry limit testing (5 pH correction attempts)
- [ ] Calendar day advancement and wraparound
- [ ] Web UI button functionality

**Estimated effort**: 8-16 hours

---

## Documentation

### 15. User Manual
**Status**: Open
**Priority**: Medium
**Description**: Create comprehensive user documentation for operators.

**Sections Needed**:
- [ ] System overview and architecture
- [ ] Hardware setup and wiring diagrams
- [ ] Initial configuration and calibration
- [ ] Daily operation procedures
- [ ] Web UI guide
- [ ] Troubleshooting common issues
- [ ] Maintenance schedule
- [ ] Safety warnings and emergency procedures

**Estimated effort**: 8-12 hours

---

### 16. API Documentation
**Status**: Open
**Priority**: Low
**Description**: Document all public API methods for developers/integrators.

**Tasks**:
- [ ] Document all PlantOSLogic public methods
- [ ] Document CalendarManager API
- [ ] Document ActuatorSafetyGate usage
- [ ] Provide integration examples
- [ ] Document ESPHome lambda patterns

**Estimated effort**: 4-8 hours

---

## Summary

**Total Open Tasks**: 16
**High Priority**: 6 tasks (~50-88 hours)
  - Complete 120-day schedule (4-8h)
  - Integrate pH sensor (8-16h)
  - Configure actuators (4-8h)
  - Calibrate dosing formula (4-8h)
  - Add temperature monitoring (2-4h) **CRITICAL**
  - Implement error handling (12-20h) **CRITICAL**
**Medium Priority**: 4 tasks (~18-36 hours)
**Low Priority**: 6 tasks (~58-92 hours)

**Estimated Total Effort**: 126-216 hours

**Recommended Next Steps** (in order):
1. Complete 120-day schedule configuration (4-8h)
2. Integrate real pH sensor (8-16h)
3. **Add temperature monitoring (2-4h) - CRITICAL for pH accuracy**
4. Configure physical actuators (4-8h)
5. Calibrate acid dosing formula (4-8h)
6. **Implement error handling and recovery (12-20h) - CRITICAL for production**
7. End-to-end testing (8-16h)

**Minimum Viable Product (MVP)**: Tasks 1-6 + 14 (~42-80 hours)
**Production Ready**: MVP + automated triggers + water level monitoring (~54-100 hours)

---

## Notes

- Tasks can be parallelized if multiple developers are working on the project
- Hardware dependencies (sensors, pumps) may extend timelines
- Calibration and testing are iterative processes
- Some tasks (like PID control) are optional enhancements
- Production deployment should include rigorous testing and safety validation

### CRITICAL PATH ITEMS

**Two tasks have been elevated to HIGH PRIORITY as they are critical for production readiness:**

1. **Temperature Monitoring (Task #5)**:
   - pH readings are temperature-dependent (±0.003 pH per °C)
   - Without temperature compensation, pH control accuracy suffers (0.1-0.3 pH error)
   - Most pH meters require temperature input for ATC (Automatic Temperature Compensation)
   - **Must be completed before pH sensor calibration**

2. **Error Handling and Recovery (Task #6)**:
   - Currently, ActuatorSafetyGate rejections may cause silent failures
   - No retry logic for transient failures
   - Incomplete sequences can leave system in inconsistent state
   - **Must be completed before production deployment**
   - Implements: Retry logic, emergency stop, error alerts, recovery strategies

These critical items add ~14-24 hours to the MVP timeline but are essential for safe and accurate operation.

---

**Last Updated**: 2025-12-04 (added temperature monitoring and error handling as critical items)
**Version**: 0.5.0
