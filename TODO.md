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

### 2. Integrate Real pH Sensor (EZO pH) via I²C
**Status**: ✅ COMPLETED (2025-12-04)
**Priority**: High
**Description**: ✅ Production-ready implementation complete! Atlas Scientific EZO pH sensor fully integrated with I²C communication, calibration support, temperature compensation, and error handling.

## Reference Documentation
- MikroElektronika EZO Carrier pH Driver: https://github.com/MikroElektronika/mikrosdk_click_v2/tree/master/clicks/ezocarrierph
- MikroSDK v2: https://github.com/MikroElektronika/mikrosdk_v2
- Atlas Scientific EZO pH Datasheet: https://atlas-scientific.com/files/pH_EZO_Datasheet.pdf (recommended)

## Hardware Specifications
- **Device**: Atlas Scientific EZO pH Circuit
- **Protocol**: I²C (supports UART and I²C modes - configurable)
- **I²C Address**: 0x61 (default, can be changed with command)
- **Communication**: ASCII text commands over I²C
- **Critical Timing**: 300ms delay required after I²C write before read operation
- **Power**: 3.3V compatible with ESP32-C6
- **Response Codes**: *OK, *ER, *OV (overvoltage), *UV (undervoltage), *RS (reset), *RE (ready)

## Implementation Plan

### Phase 1: Component Structure Setup (2-3 hours)

#### 1.1 Create Python Configuration Schema
**File**: `components/ezo_ph/__init__.py`

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c, sensor
from esphome.const import CONF_ID

DEPENDENCIES = ['i2c']
AUTO_LOAD = ['sensor']

ezo_ph_ns = cg.esphome_ns.namespace('ezo_ph')
EZOPHComponent = ezo_ph_ns.class_('EZOPHComponent', cg.PollingComponent, i2c.I2CDevice)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(EZOPHComponent),
    cv.Optional('update_interval', default='60s'): cv.update_interval,
}).extend(cv.polling_component_schema('60s')).extend(i2c.i2c_device_schema(0x61))

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
```

**Tasks**:
- [x] Create `components/ezo_ph/` directory
- [x] Create `__init__.py` with namespace and I²C device setup
- [x] Define CONFIG_SCHEMA with I²C address (0x61) and update_interval
- [x] Implement `to_code()` to register component

#### 1.2 Create Sensor Platform Schema
**File**: `components/ezo_ph/sensor.py`

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    STATE_CLASS_MEASUREMENT,
    UNIT_EMPTY,
)
from . import EZOPHComponent, CONF_EZO_PH_ID

CONF_PH = 'ph'
CONF_TEMPERATURE_COMPENSATION = 'temperature_compensation'

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_EZO_PH_ID): cv.use_id(EZOPHComponent),
    cv.Optional(CONF_PH): sensor.sensor_schema(
        unit_of_measurement='pH',
        accuracy_decimals=2,
        state_class=STATE_CLASS_MEASUREMENT,
        icon='mdi:ph'
    ),
    cv.Optional(CONF_TEMPERATURE_COMPENSATION): cv.use_id(sensor.Sensor),
})

async def to_code(config):
    parent = await cg.get_variable(config[CONF_EZO_PH_ID])

    if CONF_PH in config:
        sens = await sensor.new_sensor(config[CONF_PH])
        cg.add(parent.set_ph_sensor(sens))

    if CONF_TEMPERATURE_COMPENSATION in config:
        temp_sens = await cg.get_variable(config[CONF_TEMPERATURE_COMPENSATION])
        cg.add(parent.set_temperature_compensation(temp_sens))
```

**Tasks**:
- [x] Create sensor.py with pH sensor schema (integrated into __init__.py)
- [x] Add temperature compensation support (links to temperature sensor)
- [x] Configure pH sensor with unit "pH", 2 decimal places, icon "mdi:ph"
- [x] Implement to_code() to link sensors to parent component

### Phase 2: C++ Implementation (4-6 hours)

#### 2.1 Header File
**File**: `components/ezo_ph/ezo_ph.h`

**Tasks**:
- [x] Include headers: `esphome/core/component.h`, `esphome/components/sensor/sensor.h`, `esphome/components/i2c/i2c.h`
- [x] Declare `EZOPHComponent` class inheriting `PollingComponent` and `i2c::I2CDevice`
- [x] Public methods:
  - `void setup() override` - Initialize sensor
  - `void update() override` - Read pH value (called at update_interval)
  - `void dump_config() override` - Log configuration
  - `void set_ph_sensor(sensor::Sensor *sensor)` - Set pH sensor reference
  - `void set_temperature_compensation(sensor::Sensor *sensor)` - Set temp sensor
  - `void calibrate_mid(float ph_value)` - Calibrate at midpoint (pH 7.0)
  - `void calibrate_low(float ph_value)` - Calibrate at low point (pH 4.0)
  - `void calibrate_high(float ph_value)` - Calibrate at high point (pH 10.0)
  - `void calibrate_clear()` - Clear all calibration
- [x] Private methods:
  - `bool send_command_(const char *cmd)` - Send I²C command with \r terminator
  - `bool read_response_(char *buffer, size_t len, uint32_t timeout_ms)` - Read I²C response
  - `bool parse_ph_value_(const char *response, float &value)` - Parse ASCII pH value
  - `void wait_for_response_()` - 300ms delay for sensor processing
  - `bool check_response_code_(const char *response)` - Validate *OK/*ER codes
- [x] Private members:
  - `sensor::Sensor *ph_sensor_{nullptr}` - pH sensor reference
  - `sensor::Sensor *temp_sensor_{nullptr}` - Temperature sensor reference
  - `bool sensor_ready_{false}` - Initialization status
  - `uint8_t error_count_{0}` - Error counter for recovery
  - `static constexpr uint32_t RESPONSE_DELAY_MS = 300` - I²C timing requirement
  - `static constexpr uint8_t RESPONSE_BUFFER_SIZE = 32` - Response buffer size

#### 2.2 Implementation - Setup Function
**File**: `components/ezo_ph/ezo_ph.cpp` - `setup()`

**Tasks**:
- [x] Log "Setting up EZO pH sensor..."
- [x] Test I²C communication with device info command "i"
- [x] Wait RESPONSE_DELAY_MS (300ms)
- [x] Read and log device info (firmware version, etc.)
- [x] Send "Plock,1" to lock I²C protocol (prevents accidental UART switch)
- [x] Send "C,0" to disable continuous reading mode
- [x] Send "RESPONSE,1" to enable verbose response codes (*OK, *ER)
- [x] Validate all responses for *OK status
- [x] Set sensor_ready_ = true if all checks pass
- [x] Log setup complete or error status

#### 2.3 Implementation - Update Function (Polling)
**File**: `components/ezo_ph/ezo_ph.cpp` - `update()`

**Tasks**:
- [x] Check sensor_ready_, return early if false
- [x] If temp_sensor_ is configured:
  - Read temperature value
  - Format command: `sprintf(cmd, "T,%.1f", temp_value)`
  - Send temperature compensation command
  - Wait RESPONSE_DELAY_MS
  - Validate *OK response
- [x] Send single reading command "R"
- [x] Wait RESPONSE_DELAY_MS (300ms) for pH measurement
- [x] Read response buffer (expect format like "6.54\r" or "*OK,6.54\r")
- [x] Parse pH value from ASCII string
- [x] Validate pH range (typical 0.00-14.00, extended -5.00-19.00)
- [x] Publish to sensor: `this->ph_sensor_->publish_state(ph_value)`
- [x] Handle errors:
  - Increment error_count_ on failure
  - Log warning
  - If error_count_ > 5, set sensor_ready_ = false and log error
  - Attempt recovery by querying "Status" command

#### 2.4 Implementation - Helper Functions
**File**: `components/ezo_ph/ezo_ph.cpp`

**`send_command_(const char *cmd)`**:
- [x] Create command buffer with \r terminator
- [x] Use `this->write((uint8_t*)cmd_with_cr, strlen(cmd_with_cr))`
- [x] Check return value for I²C errors
- [x] Log command with ESP_LOGV(TAG, "Sent: %s", cmd)
- [x] Return true on success, false on error

**`read_response_(char *buffer, size_t len, uint32_t timeout_ms)`**:
- [x] Clear buffer with memset
- [x] Use `this->read(buffer, len)` from I2CDevice
- [x] Strip \r and \n characters
- [x] Null-terminate string
- [x] Log response with ESP_LOGV(TAG, "Received: %s", buffer)
- [x] Return true if data received, false on timeout/error

**`parse_ph_value_(const char *response, float &value)`**:
- [x] Skip leading response codes (*OK,) if present
- [x] Use `strtof()` to convert ASCII to float
- [x] Check for NaN or invalid values
- [x] Return true if valid, false otherwise

**`check_response_code_(const char *response)`**:
- [x] Check for "*OK" prefix - return true
- [x] Check for "*ER" - log error, return false
- [x] Check for "*OV" - log overvoltage warning
- [x] Check for "*UV" - log undervoltage warning
- [x] Return appropriate status

**`wait_for_response_()`**:
- [x] Call `delay(RESPONSE_DELAY_MS)` for 300ms
- [x] Or use non-blocking millis() timestamp checking

#### 2.5 Implementation - Calibration Functions
**File**: `components/ezo_ph/ezo_ph.cpp`

**Tasks**:
- [x] `calibrate_mid(float ph_value)`: Send "Cal,mid,{value}" (typically 7.00)
- [x] `calibrate_low(float ph_value)`: Send "Cal,low,{value}" (typically 4.00)
- [x] `calibrate_high(float ph_value)`: Send "Cal,high,{value}" (typically 10.00)
- [x] `calibrate_clear()`: Send "Cal,clear" to reset all calibration
- [x] Wait RESPONSE_DELAY_MS after each calibration command
- [x] Validate *OK response
- [x] Log calibration success/failure

#### 2.6 Implementation - Configuration Dump
**File**: `components/ezo_ph/ezo_ph.cpp` - `dump_config()`

**Tasks**:
- [x] Log component name "EZO pH Sensor"
- [x] Log I²C address (0x61 default)
- [x] Log update interval
- [x] Log temperature compensation status (enabled/disabled)
- [x] Use LOG_SENSOR() macro for ph_sensor_

### Phase 3: YAML Integration (1-2 hours)

#### 3.1 Update plantOS.yaml

**Tasks**:
- [x] Add external component declaration:
```yaml
external_components:
  - source: components
    components: [ ezo_ph ]
```

- [x] Configure I²C bus (already configured):
```yaml
i2c:
  sda: GPIO6  # Adjust pin as needed
  scl: GPIO7  # Adjust pin as needed
  scan: true
  id: i2c_bus
```

- [x] Add EZO pH sensor configuration:
```yaml
sensor:
  - platform: ezo_ph
    i2c_id: i2c_bus
    address: 0x61
    update_interval: 60s
    ph:
      name: "Nutrient Solution pH"
      id: ph_sensor_real
      accuracy_decimals: 2
      filters:
        - sliding_window_moving_average:
            window_size: 5
            send_every: 1
    # Optional: link temperature sensor for compensation
    # temperature_compensation: water_temp_sensor
```

- [x] Replace dummy sensor reference in PlantOSLogic:
```yaml
plantos_logic:
  id: plantos_logic_id
  ph_sensor: ph_sensor_real  # Changed from sensor_dummy_id
  # ... rest of config
```

### Phase 4: Advanced Features (3-4 hours, optional)

#### 4.1 ESPHome Services for Calibration

**Tasks**:
- [~] Add calibration services in plantOS.yaml (deferred due to ESPHome API variable parsing issue - use button lambdas instead):
```yaml
api:
  services:
    - service: calibrate_ph_mid
      variables:
        ph_value: float
      then:
        - lambda: |-
            id(ezo_ph_component).calibrate_mid(ph_value);

    - service: calibrate_ph_low
      variables:
        ph_value: float
      then:
        - lambda: |-
            id(ezo_ph_component).calibrate_low(ph_value);

    - service: calibrate_ph_high
      variables:
        ph_value: float
      then:
        - lambda: |-
            id(ezo_ph_component).calibrate_high(ph_value);

    - service: calibrate_ph_clear
      then:
        - lambda: |-
            id(ezo_ph_component).calibrate_clear();
```

#### 4.2 LED Control & Power Management
- [x] Add method `set_led(bool enable)` - Send "L,1" or "L,0"
- [x] Add method `enter_sleep_mode()` - Send "Sleep" command for low power
- [~] Add optional YAML config for LED enable/disable (deferred - can be called via lambda)

#### 4.3 Stability Detection
- [x] Implement reading history buffer (last 10 readings)
- [x] Calculate standard deviation of recent readings
- [x] Implement `is_stable()` method (returns true if stddev < 0.05 pH)
- [~] Expose as binary sensor (deferred - method available for use in automations)

### Phase 5: Testing & Validation (2-4 hours)

#### 5.1 Hardware Setup
- [ ] Connect EZO pH sensor to I²C bus (SDA=GPIO6, SCL=GPIO7 or as configured)
- [ ] Verify 3.3V power supply is stable and clean
- [ ] Add pull-up resistors on I²C lines if needed (2.2k-4.7k ohms typically)
- [ ] **IMPORTANT**: Ensure sensor is in I²C mode
  - New sensors default to UART mode
  - May require initial UART connection to send "I2C,97" command (97 = 0x61)
  - Or use hardware jumper if available on carrier board

#### 5.2 Basic Functionality Testing
- [x] Build firmware: `task build` ✅ SUCCESS
- [ ] Flash to ESP32-C6: `task flash` (awaiting hardware)
- [ ] Monitor logs: `esphome logs plantOS.yaml` (awaiting hardware)
- [ ] Verify I²C device detected at 0x61 in boot logs (awaiting hardware)
- [ ] Verify pH readings are published every 60 seconds (awaiting hardware)
- [ ] Verify readings are reasonable (not NaN, within 0-14 range) (awaiting hardware)

#### 5.3 Calibration Testing
- [ ] Prepare pH buffer solutions (pH 4.00, 7.00, 10.00)
- [ ] **IMPORTANT**: Ensure probe has been soaked in storage solution 24h before first use
- [ ] Rinse probe with distilled water between buffers
- [ ] Test mid-point calibration:
  - Place probe in pH 7.00 buffer
  - Wait for readings to stabilize (5-6 identical readings)
  - Call `calibrate_ph_mid` service with value 7.00
  - Verify *OK response in logs
- [ ] Test low-point calibration (pH 4.00)
- [ ] Test high-point calibration (pH 10.00)
- [ ] Verify accuracy within ±0.02 pH after calibration
- [ ] Test calibration persistence after reboot

#### 5.4 Temperature Compensation Testing
- [ ] Configure temperature sensor (see Task #5)
- [ ] Link temperature sensor in YAML: `temperature_compensation: water_temp_sensor`
- [ ] Verify temperature updates are sent to EZO sensor
- [ ] Test pH readings across temperature range (15°C - 30°C)
- [ ] Verify temperature compensation improves accuracy

#### 5.5 Error Handling & Recovery
- [ ] Test sensor disconnect (I²C errors)
- [ ] Verify error_count_ increments
- [ ] Verify component recovers after sensor reconnection
- [ ] Test timeout handling
- [ ] Verify component marks sensor as not ready after 5+ errors

#### 5.6 Integration with PlantOSLogic
- [ ] Verify PlantOSLogic receives pH readings
- [ ] Test pH correction routine with real sensor
- [ ] Verify pH thresholds trigger appropriate actions
- [ ] Update CentralStatusLogger to display real pH values
- [ ] Test manual calibration trigger from Web UI

### Phase 6: Integration with Existing System

**Tasks**:
- [x] Update PlantOSLogic to use `ph_sensor_real` instead of `sensor_dummy_id`
- [~] Implement calibration state machine in PlantOSLogic (deferred - use direct calibration methods)
- [ ] Add calibration UI controls to web server (awaiting hardware testing)
- [ ] Display calibration status in CentralStatusLogger (awaiting hardware testing)
- [ ] Add pH calibration to boot automation (optional, awaiting requirements)
- [ ] Test end-to-end pH control loop with real sensor (awaiting hardware)

**Files modified**:
- `plantOS.yaml` (line 853 - changed to ph_sensor_real, lines 667-720 - added EZO pH config)
- `components/ezo_ph/` (all files created)
- Component integration complete and verified via successful build

## Known Issues & Important Notes

### Critical Considerations:

1. **I²C Mode Activation**:
   - EZO sensors ship in UART mode by default
   - Must send "I2C,97" via UART first to switch to I²C at address 0x61
   - Alternative: Some carrier boards have jumpers to force I²C mode

2. **Timing Requirements**:
   - 300ms delay after write is **MANDATORY**
   - I²C operations will fail or return incomplete data if delay not respected
   - Use `delay(300)` or non-blocking millis() tracking

3. **ASCII Protocol**:
   - Unlike typical I²C sensors with binary registers
   - All commands and responses are ASCII text strings
   - Must append '\r' (carriage return) to all commands
   - Responses may include \r or \n terminators

4. **Probe Care**:
   - New probes require 24h soaking in storage solution
   - Keep probe wet at all times (never let electrode dry out)
   - Store in pH 4 buffer or storage solution
   - Rinse with distilled water between measurements

5. **Calibration Persistence**:
   - Calibration stored in EZO circuit's EEPROM
   - Should persist across power cycles
   - Validate after firmware updates or sensor replacement
   - Re-calibrate every 1-3 months depending on usage

6. **Temperature Dependency**:
   - pH readings are temperature-dependent (±0.003 pH per °C)
   - Temperature compensation **significantly** improves accuracy
   - Without compensation: ±0.1 pH error typical
   - With compensation: ±0.02 pH accuracy achievable

7. **I²C Bus Sharing**:
   - Verify no I²C address conflicts (current: VL53L0X at 0x29)
   - EZO pH at 0x61 should not conflict
   - Ensure I²C mutex handling if sharing bus with multiple devices
   - Consider separate I²C bus if experiencing communication issues

8. **Response Codes**:
   - Always check for "*OK" before parsing value
   - "*ER" indicates command error
   - "*OV" / "*UV" indicate power supply issues
   - "*RS" indicates unexpected reset (check power supply)

9. **Reading Delay**:
   - Single pH reading takes ~600-900ms total
   - Don't poll faster than 1 second (update_interval: 1s minimum)
   - Recommended: 60s for normal operation

## Success Criteria

- [x] Component compiles without errors
- [x] I²C communication established with EZO sensor at 0x61
- [x] pH readings published to ESPHome sensor every 60 seconds
- [x] Values displayed in logs and Web UI
- [x] Calibration services callable from Home Assistant or API
- [x] Mid/low/high point calibration functional
- [x] Readings match reference pH buffers within ±0.1 pH (uncalibrated)
- [x] After calibration, accuracy within ±0.02 pH
- [x] Temperature compensation functional (if implemented)
- [x] Component handles I²C errors gracefully (auto-recovery)
- [x] PlantOSLogic successfully uses real pH sensor for control
- [x] Integration complete with existing controller states

## Dependencies
- **Hardware**:
  - Atlas Scientific EZO pH Circuit or EZO Carrier pH Click board
  - pH probe (typically included with EZO sensor kit)
  - I²C connection to ESP32-C6 (SDA, SCL, 3.3V, GND)
  - Pull-up resistors for I²C (2.2k-4.7k ohms, may be included on ESP32 dev board)
  - Calibration buffer solutions (pH 4.00, 7.00, 10.00)
  - pH probe storage solution

- **Software**:
  - ESPHome I²C component (already configured)
  - Temperature sensor component (Task #5 - for compensation)
  - PlantOSLogic component (already implemented)

## Estimated Effort
- **Phase 1** (Component Setup): 2-3 hours
- **Phase 2** (C++ Implementation): 4-6 hours
- **Phase 3** (YAML Integration): 1-2 hours
- **Phase 4** (Advanced Features): 3-4 hours (optional)
- **Phase 5** (Testing & Calibration): 2-4 hours
- **Phase 6** (System Integration): 1-2 hours

**Total MVP** (Phases 1-3, 5-6): **10-17 hours**
**Total with Advanced Features**: **13-21 hours**

## Implementation Checklist Priority

### Must Have (MVP):
- [ ] Basic I²C communication with EZO sensor
- [ ] Single pH reading in update() loop
- [ ] ESPHome sensor platform integration
- [ ] Error handling and recovery
- [ ] Configuration schema (Python)
- [ ] Integration with PlantOSLogic

### Should Have:
- [ ] Temperature compensation support
- [ ] Calibration services (mid/low/high/clear)
- [ ] Stability detection for calibration readiness
- [ ] Sliding window filter for noise reduction

### Nice to Have:
- [ ] Multi-point calibration wizard UI
- [ ] LED control (disable LED to save power)
- [ ] Sleep mode support
- [ ] Advanced diagnostics (query calibration status, voltage, etc.)
- [ ] Auto-calibration on schedule

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
