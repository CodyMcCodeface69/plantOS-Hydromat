# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

PlantOS is a sophisticated ESP32-C6 based hydroponic plant monitoring and control system built on ESPHome. The project implements **~10,000 lines of code** organized into **16 custom components** that manage pH correction, nutrient dosing, water management, and system safety. The architecture follows a layered design with clear separation between hardware control, application logic, and multi-layer safety systems.

### Key Statistics
- **Total Lines of Code**: ~10,000
- **Custom Components**: 16
- **Programming Languages**: C++ (implementation), Python (ESPHome integration), YAML (configuration), Nix (environment)
- **Main Configuration**: 942 lines (plantOS.yaml)

## Development Environment

This project uses Nix flakes with direnv for reproducible development environments.

### Setup
```bash
# Enter development environment (if using direnv)
direnv allow

# Or manually enter Nix shell
nix develop
```

The development shell includes: esphome, python3, cmake, ninja, and other required dependencies.

## Common Commands

All commands are managed via Taskfile (Task runner). The entry point is `plantOS.yaml`.

```bash
# Build the firmware
task build

# Flash to MCU
task flash

# Build, flash, and attach to log stream
task run

# Clean cached build files
task clean
```

Underlying ESPHome commands (if needed directly):
```bash
esphome compile plantOS.yaml
esphome upload plantOS.yaml
esphome run plantOS.yaml
esphome clean plantOS.yaml
```

## System Architecture

### Architecture Layers

PlantOS uses a 5-layer architecture for clear separation of concerns:

1. **Layer 1: Infrastructure** (Foundation)
   - Hardware-level FSM with visual feedback
   - Unified status reporting and monitoring

2. **Layer 2: Safety & Persistence** (Critical Systems)
   - Centralized actuator safety controls
   - Power-loss recovery via NVS storage
   - Hardware watchdog timer

3. **Layer 3: Application Logic** (Business Rules)
   - Main application FSM for routine orchestration
   - 120-day grow cycle schedule management

4. **Layer 4: Sensors & I/O** (Hardware Interfaces)
   - pH sensor driver with calibration
   - Sensor filtering and validation
   - I2C bus diagnostics

5. **Layer 5: Utilities** (Supporting Components)
   - Testing and development helpers

### Configuration Files

- `plantOS.yaml`: Main ESPHome configuration (942 lines) defining hardware, components, and their interconnections
- `secrets.yaml`: WiFi credentials and other secrets (gitignored, see `secrets.example.yaml`)
- `flake.nix`: Nix development environment and build configuration
- `Taskfile.yml`: Task automation definitions

### Data Flow Overview

```
I2C Bus (pH Sensor)
  → Sensor Filter (outlier rejection)
    → Controller FSM (LED feedback) + PlantOS Logic (routines)
      → Calendar Manager (get schedule)
        → Actuator Safety Gate (validate + execute)
          → Persistent State Manager (log critical events)
            → Hardware (pumps, valves, etc.)
```

### Custom Components

Components are located in `components/` and follow ESPHome's external component structure. Each component has:
- Python code (`__init__.py`, `*.py`): ESPHome code generation and YAML config schema
- C++ code (`*.h`, `*.cpp`): Runtime implementation for ESP32

## Component Reference

### Layer 1: Infrastructure Components

#### controller

**Purpose**: Hardware-level finite state machine for system status and visual feedback
**Location**: `components/controller/`
**LOC**: ~800

**Files:**
- `__init__.py`: Config schema requiring `sensor_source` and `light_target`
- `controller.h/cpp`: Core FSM logic (setup, loop, helper functions)
- `state_init.cpp`: INIT state (boot sequence: red → yellow → green, 3s)
- `state_calibration.cpp`: CALIBRATION state (blinking yellow, 4s)
- `state_ready.cpp`: READY state (breathing green, continuous)
- `state_error.cpp`: ERROR state (fast red flashing, 5s)
- `state_error_test.cpp`: ERROR_TEST state (blue/purple pulse for PSM testing)
- `CentralStatusLogger.h/cpp`: Unified status reporting system

**Architecture:**
- Function pointer-based FSM (no vtable overhead)
- State transitions via returned next state
- Non-blocking timing with `millis()`
- Runs at ~1000 Hz in main loop

**State Diagram:**
```
INIT (3s) → CALIBRATION (4s) → READY (continuous)
                                   ↓ (sensor > 90)
                                ERROR (5s)
                                   ↓
                                INIT (restart)
```

**Usage:**
```yaml
controller:
  id: my_logic_controller
  sensor_source: filtered_ph_sensor  # Any ESPHome sensor
  light_target: system_led           # Any ESPHome light
  state_text: controller_state       # Optional text sensor for UI
```

#### central_status_logger

**Purpose**: Unified logging and monitoring system
**Location**: `components/central_status_logger/` (embedded in controller)
**LOC**: ~400

**Features:**
- Multiple simultaneous alert tracking
- Structured 30-second status reports
- Network status (IP, web server)
- Sensor data aggregation
- FSM state reporting
- I2C hardware status
- Easter egg mode (420 ASCII art at 4:20 AM/PM)

**Alert Structure:**
```cpp
struct Alert {
    std::string type;        // "SPILL", "PH_CRITICAL", etc.
    std::string reason;      // Detailed description
    uint32_t timestamp;      // When triggered
};
```

**Status Report Format:**
```
================================================================================
  PLANTOS SYSTEM STATUS REPORT
================================================================================
System Time: 2025-12-05 14:23:45

--- NETWORK STATUS ---
  IP Address: 192.168.1.100
  Web Server: ONLINE

--- SENSOR DATA ---
  Filtered pH: 6.85

--- SYSTEM STATE ---
  Controller: READY
  Logic: IDLE
  Maintenance Mode: DISABLED

--- ALERT STATUS ---
  Status: ALL CLEAR
================================================================================
```

### Layer 2: Safety and Persistence Components

#### actuator_safety_gate

**Purpose**: Centralized safety layer for all actuators
**Location**: `components/actuator_safety_gate/`
**LOC**: ~500

**Safety Features:**
1. **Debouncing**: Prevents redundant commands
2. **Duration Limits**: Enforces max runtime per actuator
3. **Soft-Start/Soft-Stop**: PWM ramping (0-100% over configurable time)
4. **Runtime Tracking**: Monitors active duration
5. **Violation Logging**: Logs all rejections with reasons

**Actuator IDs** (used throughout system):
- `AcidPump` - pH down dosing
- `NutrientPumpA/B/C` - Nutrient dosing
- `WaterValve` - Fresh water inlet
- `WastewaterPump` - Tank drainage
- `AirPump` - Mixing and aeration

**API Example:**
```cpp
// Request pump ON for 10 seconds
if (safety_gate->executeCommand("AcidPump", true, 10)) {
    // Approved - activate pump
} else {
    // Rejected - logged automatically
}
```

**Configuration:**
```yaml
actuator_safety_gate:
  id: safety_gate
  acid_pump_max_duration: 30         # seconds
  nutrient_pump_max_duration: 60
  water_valve_max_duration: 300
  wastewater_pump_max_duration: 180
  air_pump_max_duration: 600
  ramp_duration: 2000                # milliseconds for soft-start/stop
```

#### persistent_state_manager

**Purpose**: Critical event persistence across power cycles
**Location**: `components/persistent_state_manager/`
**LOC**: ~250

**NVS Storage**: Uses ESP32 Non-Volatile Storage for crash recovery

**Event Structure:**
```cpp
struct CriticalEventLog {
    char eventID[32];       // "DOSING_ACID", "WATERING", etc.
    int64_t timestampSec;   // Unix timestamp
    int32_t status;         // 0=STARTED, 1=COMPLETED, 2=ERROR
};
```

**Usage Pattern:**
```cpp
// Before critical operation
psm->logEvent("DOSING_ACID", 0);  // Log STARTED
turnOnAcidPump();

// After completion
turnOffAcidPump();
psm->clearEvent();  // Clear - operation complete

// On boot - check for recovery
if (psm->wasInterrupted(60)) {  // Within last 60s
    // Take recovery action
}
```

#### wdt_manager

**Purpose**: Hardware watchdog timer management
**Location**: `components/wdt_manager/`
**LOC**: ~150

**Operation:**
- Initialize WDT with 10-second timeout
- Feed every 500ms during normal operation
- Hardware reset if feeding stops (crash detection)
- Optional test mode for validation

### Layer 3: Application Logic Components

#### plantos_logic

**Purpose**: Main application FSM for routine orchestration
**Location**: `components/plantos_logic/`
**LOC**: ~600

**FSM States:**
```cpp
enum class LogicStatus {
    IDLE,                     // Waiting for trigger
    PH_CORRECTION_DUE,        // pH sequence triggered
    PH_MEASURING,             // Stabilization (5 min)
    PH_CALCULATING,           // Determine dosing
    PH_INJECTING,             // Acid pump active
    PH_MIXING,                // Air pump mixing (2 min)
    PH_CALIBRATING,           // Calibration routine
    FEEDING_DUE,              // Nutrient sequence triggered
    FEEDING_INJECTING,        // Nutrient pumps active
    WATER_MANAGEMENT_DUE,     // Water task triggered
    WATER_FILLING,            // Fresh water valve open
    WATER_EMPTYING,           // Wastewater pump active
    AWAITING_SHUTDOWN         // Maintenance mode
};
```

**pH Correction Sequence** (Critical Flow):
```
1. PH_MEASURING (5 min)
   - All pumps OFF
   - Read pH until stable

2. PH_CALCULATING
   - Compare pH with target range
   - Calculate required acid duration
   - If in range → IDLE
   - Else → PH_INJECTING

3. PH_INJECTING
   - Air pump ON (mixing)
   - Acid pump ON (via SafetyGate)
   - Wait duration + 200ms margin

4. PH_MIXING
   - Air pump ON only
   - Wait 2 minutes
   - Loop back to PH_MEASURING (max 5 attempts)
```

**Dependencies:**
- `ActuatorSafetyGate` - All actuator control
- `PersistentStateManager` - Recovery logging
- `CalendarManager` - Daily schedule
- `CentralStatusLogger` - Status reporting

#### calendar_manager

**Purpose**: 120-day grow cycle schedule management
**Location**: `components/calendar_manager/`
**LOC**: ~300

**Schedule Structure:**
```cpp
struct DailySchedule {
    uint8_t day_number;                 // 1-120
    float target_ph_min;                // Lower pH bound
    float target_ph_max;                // Upper pH bound
    uint32_t nutrient_A_duration_ms;    // Dosing time A
    uint32_t nutrient_B_duration_ms;    // Dosing time B
    uint32_t nutrient_C_duration_ms;    // Dosing time C
};
```

**Features:**
- JSON schedule parsing from YAML
- NVS persistence of current day
- Safe mode toggle (disables automation)
- Verbose logging mode

### Layer 4: Sensor and I/O Components

#### ezo_ph

**Purpose**: Production pH sensor (Atlas Scientific EZO circuit)
**Location**: `components/ezo_ph/`
**LOC**: ~350

**I2C Protocol**: ASCII text commands (not binary registers)
**Critical Timing**: 300ms delay after write before reading
**I2C Address**: 0x61 (configurable)

**Features:**
- Temperature compensation
- Three-point calibration (mid/low/high)
- Stability detection
- Sliding window averaging
- Range validation (pH 0.0-14.0)

**Calibration Methods:**
```cpp
void calibrate_mid(float ph_value);   // pH 7.00 buffer
void calibrate_low(float ph_value);   // pH 4.00 buffer
void calibrate_high(float ph_value);  // pH 10.00 buffer
```

**Usage:**
```yaml
sensor:
  - platform: ezo_ph
    id: raw_ph_sensor
    name: "Raw pH"
    i2c_id: i2c_bus
    address: 0x61
    update_interval: 5s
```

#### sensor_filter

**Purpose**: Robust averaging with outlier rejection
**Location**: `components/sensor_filter/`
**LOC**: ~150

**Algorithm:**
1. Collect readings in window (default: 20)
2. Sort values
3. Reject percentage from each end (default: 10%)
4. Average remaining middle values
5. Publish filtered result

**Configuration:**
```yaml
sensor:
  - platform: sensor_filter
    id: filtered_ph_sensor
    name: "Filtered pH"
    sensor_source: raw_ph_sensor
    window_size: 20
    reject_percentage: 0.10  # Reject 10% lowest + 10% highest
```

#### sensor_dummy

**Purpose**: Development/testing sensor
**Location**: `components/sensor_dummy/`
**LOC**: ~100

**Behavior**: Cycles 0→10→20→...→100→0
**Update Interval**: 1 second
**Use Case**: Controller FSM development and testing

#### i2c_scanner

**Purpose**: I2C bus diagnostics and device validation
**Location**: `components/i2c_scanner/`
**LOC**: ~200

**Scan Range**: 0x01 to 0x77 (valid 7-bit addresses)

**Features:**
- Boot-time scan
- Periodic scanning (configurable interval)
- Critical device validation
- Integration with status logger

**Common Device Addresses:**
- `0x48`: ADS1115 (16-bit ADC)
- `0x20`: MCP23017 (GPIO expander)
- `0x76`: BME280 (environmental sensor)
- `0x68`: DS3231 (RTC)
- `0x61`: EZO pH circuit

### Layer 5: Utility Components

#### time_logger

**Purpose**: NTP time logging demonstration
**Location**: `components/time_logger/`
**Output Format**: `dd.mm.yyyy HH:MM:SS`
**Interval**: Configurable (default: 1 min)

#### ip_logger

**Purpose**: Network status logging
**Location**: `components/ip_logger/`
**Status**: Currently disabled (superseded by CentralStatusLogger)

#### i2c_mutex_demo

**Purpose**: FreeRTOS mutex for thread-safe I2C access
**Location**: `components/i2c_mutex_demo/`
**Global Mutex**: `I2CMutexDemo::i2c_mutex_`
**Production Mode**: Mutex only (no test overhead)

#### dummy_actuator_trigger

**Purpose**: ActuatorSafetyGate testing component
**Location**: `components/dummy_actuator_trigger/`

**Test Sequences:**
1. Debouncing validation
2. Max duration enforcement
3. Normal operation cycles

#### psm_checker

**Purpose**: PSM recovery testing
**Location**: `components/psm_checker/`

**Test Flow:**
1. Log "PSM_TEST" event
2. Trigger ERROR_TEST state (blue/purple LED)
3. Prompt user to unplug device
4. Verify event recovery after reboot

## Safety Architecture

PlantOS implements a multi-layer safety design:

### Layer 1: Application Logic (PlantOSLogic)
- Sequence validation
- State machine flow control
- pH range checking (software limits: 5.0-7.5)

### Layer 2: ActuatorSafetyGate
- Debouncing (prevent redundant commands)
- Duration limits (prevent overruns)
- Soft-start/soft-stop (inrush protection)
- Runtime tracking with violation logging

### Layer 3: PersistentStateManager
- Critical event logging (NVS)
- Power-loss recovery
- Operation age validation

### Layer 4: Hardware Watchdog (WDTManager)
- System hang detection
- Automatic reset (10s timeout)
- Hardware-enforced (ESP-IDF TWDT)

### Additional Safety Features
- **I2C Mutex**: Thread-safe bus access (prevents race conditions)
- **Alert System**: Multi-alert tracking with visual banners
- **Maintenance Mode**: Emergency shutdown with persistent state
- **Sensor Validation**: Range checking and outlier rejection

## Web Interface and Manual Control

### Manual Control Buttons

**System Control**:
- Reset controller FSM
- Reconnect WiFi

**Security & Modes**:
- Toggle maintenance mode (persistent shutdown)
- Toggle safe mode (disable automation)
- Toggle verbose logging

**pH Management**:
- Measure pH only (no correction)
- Start pH correction sequence
- Calibrate pH sensor (3-point)

**Primary Routines**:
- Start feeding sequence

**Water Management**:
- Fill tank
- Empty tank

### Status Monitoring

**Text Sensors:**
- Controller state (INIT, READY, ERROR, etc.)
- PlantOS Logic state (IDLE, PH_MEASURING, etc.)
- Safe mode status
- Verbose mode status

**Switches:**
- Maintenance mode (persistent shutdown)

**Periodic Status Reports** (every 30 seconds):
- Network status (IP, web server)
- Sensor data
- System state
- Alert status

## Component Development Patterns

### Python Configuration Schema

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor

my_ns = cg.esphome_ns.namespace('my_component')
MyComponent = my_ns.class_('MyComponent', cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MyComponent),
    cv.Required("sensor_source"): cv.use_id(sensor.Sensor),
    cv.Optional("update_interval", default="5s"): cv.time_period
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    sensor_var = await cg.get_variable(config["sensor_source"])
    cg.add(var.set_sensor_source(sensor_var))
```

### C++ Component Structure

```cpp
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

class MyComponent : public Component {
public:
    void setup() override {
        // Initialize once at boot
        ESP_LOGI(TAG, "Component initialized");
    }

    void loop() override {
        // Run continuously (~1000 Hz)
        // MUST be non-blocking!
    }

    void set_sensor_source(sensor::Sensor* s) {
        sensor_source_ = s;
    }

private:
    static constexpr const char* TAG = "my_component";
    sensor::Sensor* sensor_source_{nullptr};
};
```

### Non-Blocking Timing Pattern

**Anti-Pattern** (Blocks entire system):
```cpp
void loop() {
    digitalWrite(PIN, HIGH);
    delay(1000);  // BAD: Blocks WiFi, OTA, everything!
    digitalWrite(PIN, LOW);
}
```

**Correct Pattern** (Non-blocking):
```cpp
class MyComponent : public Component {
public:
    void loop() override {
        uint32_t now = millis();
        if (now - last_time_ >= interval_) {
            toggle_state();
            last_time_ = now;
        }
        // Return immediately, system stays responsive
    }

private:
    uint32_t last_time_{0};
    uint32_t interval_{1000};
};
```

### Component Linking Pattern

**YAML Configuration:**
```yaml
sensor:
  - id: my_sensor
    platform: sensor_dummy

controller:
  id: my_controller
  sensor_source: my_sensor  # Link by ID
```

**Generated Code Flow:**
1. Python validates `sensor_source` references valid ID
2. Python generates: `cg.add(controller.set_sensor_source(my_sensor))`
3. C++ receives pointer via setter
4. Component can now access sensor data

### Best Practices

1. **Always use non-blocking timing** - Never use `delay()`
2. **Log appropriately** - Use ESP_LOGD/I/W/E with meaningful messages
3. **Validate inputs** - Check pointers before use
4. **Handle errors gracefully** - Don't crash on invalid data
5. **Use const correctness** - Mark read-only data as const
6. **Document state machines** - Clearly define states and transitions
7. **Thread safety** - Use mutexes for shared resources (I2C, SPI, etc.)

## Hardware Configuration

### ESP32-C6-DevKitC-1 Specifications

- **MCU**: ESP32-C6 (RISC-V, 160 MHz)
- **Framework**: ESP-IDF (official Espressif support)
- **Built-in LED**: WS2812 RGB on GPIO8
- **USB**: USB Serial/JTAG (logging interface)
- **Flash**: ~2.5 MB (firmware + components)
- **RAM**: ~150 KB (runtime)
- **NVS**: ~20 KB (persistent state)

### I2C Bus Configuration

```yaml
i2c:
  id: i2c_bus
  sda: GPIO6              # Serial Data Line
  scl: GPIO7              # Serial Clock Line
  frequency: 100kHz       # Standard mode
  scan: false             # Use custom scanner
```

**Important**: External 4.7kΩ pull-up resistors required on SDA/SCL to 3.3V

### Network Configuration

**WiFi**:
- SSID/Password from `secrets.yaml`
- Static IP option available
- Power save mode: none
- Fast connect: enabled
- Output power: 8.5dB (mesh compatibility)
- BTM/RRM disabled (802.11v/k features)

**Fallback AP** (activated if main WiFi unavailable):
- SSID: `PlantOS-Fallback`
- Password: `debug123`
- Access logs at `http://192.168.4.1`

**Services**:
- **OTA Updates**: ESPHome platform
- **Web Server**: Port 80, version 2
- **ESPHome API**: For Home Assistant integration

### Time Synchronization

```yaml
time:
  - platform: sntp
    servers: [0.pool.ntp.org, 1.pool.ntp.org, 2.pool.ntp.org]
    timezone: "CET-1CEST,M3.5.0,M10.5.0/3"
    update_interval: 15min
```

## Key Architectural Decisions

### Why Function Pointer FSM (Controller)

**Advantages**:
- Direct function calls (no vtable overhead)
- Clean state transitions (explicit return values)
- Memory efficient on embedded systems

**Trade-offs**:
- Less type-safe than enums
- Harder to visualize transitions

### Why Separate Controller and PlantOSLogic FSMs

**Controller** (Hardware Level):
- Fast (1000 Hz)
- Visual feedback (LED patterns)
- Hardware state management
- Boot sequences, error indication

**PlantOSLogic** (Application Level):
- Slow (minute-scale operations)
- Chemical dosing sequences
- Business logic (pH correction, feeding)
- Safety-critical operations

**Benefit**: Clear separation of concerns enables independent testing and development.

### Why Centralized ActuatorSafetyGate

**Benefits**:
- Single point of control for all actuators
- Consistent safety enforcement across entire system
- Easy to add global safety rules
- Comprehensive logging of all actions

**Alternative Considered**: Per-actuator safety checks
**Rejected Because**: Scattered logic, inconsistent enforcement, harder to audit

### Why NVS for Persistence

**Benefits**:
- Survives power loss
- No external storage required
- Flash wear leveling built-in
- Atomic operations

**Use Cases**:
- Critical event logging (PSM)
- Current grow day (CalendarManager)
- Maintenance mode state

## Testing and Validation

### Hardware Testing

No automated tests currently exist. Testing is performed by:

1. **Build and Flash**: Deploy to hardware via `task run`
2. **LED Observation**: Verify controller FSM states
3. **Log Analysis**: Monitor ESP_LOG output via serial/USB
4. **Web Interface**: Use port 80 UI for manual control
5. **Component Testing**: Use dedicated test components:
   - `dummy_actuator_trigger` - SafetyGate validation
   - `psm_checker` - Recovery testing
   - `sensor_dummy` - FSM development

### Debugging Strategies

**Serial Logging**:
```bash
task run              # Build, flash, and attach to logs
task snoop            # Attach to logs only (no build/flash)
```

**Log Levels**:
- `ESP_LOGD(TAG, ...)` - Debug (verbose mode)
- `ESP_LOGI(TAG, ...)` - Info (normal operation)
- `ESP_LOGW(TAG, ...)` - Warning (degraded but functional)
- `ESP_LOGE(TAG, ...)` - Error (critical issues)

**Web Interface**:
- Access at `http://<device-ip>`
- View sensor values in real-time
- Trigger manual operations
- Monitor text sensor states

**I2C Diagnostics**:
- Use `i2c_scanner` component for bus validation
- Check CentralStatusLogger for device status
- Verify pull-up resistors (4.7kΩ to 3.3V)

## Troubleshooting Common Issues

### WiFi Not Connecting
1. Verify `secrets.yaml` credentials
2. Check WiFi power setting (8.5dB default)
3. Use fallback AP: Connect to `PlantOS-Fallback` / `debug123`
4. Check serial logs for connection errors

### I2C Device Not Found
1. Run `i2c_scanner` to detect devices
2. Verify pull-up resistors (4.7kΩ required)
3. Check wiring: GPIO6 (SDA), GPIO7 (SCL)
4. Reduce I2C frequency if long wires: `frequency: 50kHz`

### pH Sensor Not Reading
1. Wait 300ms after I2C write before reading (EZO timing requirement)
2. Verify address: `0x61` (default)
3. Check calibration status
4. Ensure sensor is in continuous mode

### Actuator Not Activating
1. Check ActuatorSafetyGate logs for rejection reason
2. Verify duration within max limits
3. Check debouncing (wait 1s between commands)
4. Ensure maintenance mode is OFF

### System Crashes/Reboots
1. Check watchdog logs (WDTManager)
2. Look for PSM recovery events on boot
3. Verify no blocking `delay()` calls in custom code
4. Check for stack overflow (increase stack size if needed)

## Future Enhancement Possibilities

Based on the current architecture, potential enhancements include:

1. **Additional Sensors**: Temperature, EC/TDS, water level, dissolved oxygen
2. **Advanced pH Control**: PID control algorithm for precise correction
3. **Remote Monitoring**: MQTT integration for cloud dashboard
4. **Automated Schedules**: Time-based routine triggers (cron-like)
5. **Data Logging**: InfluxDB/Grafana integration for historical data
6. **Mobile App**: Dedicated control interface with push notifications
7. **Multi-Tank Support**: Expand to multiple grow chambers
8. **Machine Learning**: Predictive maintenance and optimization
9. **Camera Integration**: ESP32-CAM for visual monitoring
10. **Nutrient Profiles**: Pre-configured schedules for different plant types
