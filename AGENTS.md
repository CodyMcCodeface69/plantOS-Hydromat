# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

PlantOS is a sophisticated ESP32-C6 based hydroponic plant monitoring and control system built on ESPHome. The project implements **~10,000 lines of code** organized into **15 custom components** that manage pH correction, nutrient dosing, water management, and system safety. The architecture follows a **3-layer HAL (Hardware Abstraction Layer) design** with clear separation between the unified controller (Layer 1), safety gate (Layer 2), and hardware abstraction (Layer 3).

### Key Statistics
- **Total Lines of Code**: ~10,000
- **Custom Components**: 18 (15 core + 3 utility)
- **Programming Languages**: C++ (implementation), Python (ESPHome integration), YAML (configuration), Nix (environment)
- **Main Configuration**: 942 lines (plantOS.yaml)
- **Architecture**: 3-layer HAL (Controller → SafetyGate → HAL)
- **Project Status**: 85% Complete - Architecture & Sensors Working
- **Current Phase**: MVP Finalization

### Project Status (2025-12-23)

**Current Branch**: `hydro` (Hydromat Mk.1)

**✅ What's Working**:
- 3-layer HAL architecture fully implemented
- Unified controller FSM with 12 states + LED behavior system
- EZO pH UART sensor (working, commits 8009973, 7d9d358)
- DS18B20 temperature sensor with compensation
- ActuatorSafetyGate with debouncing, duration limits, soft-start/stop
- Persistent State Manager (crash recovery via NVS)
- Web interface with manual control buttons
- pH correction, feeding, water management sequences (logic complete)

**Critical Blockers** (preventing hardware testing):
1. ✅ **GPIO Actuator Wiring** - COMPLETE: All 7 actuators configured (GPIO11, 18-23), HAL fully wired
2. ⚠️ **Water Level Sensors** - 2x XKC-Y23-V not configured; needs GPIO10-11 (requires relocating temp sensor & air pump) (3-4 hours)
3. ⚠️ **pH Dosing Calibration** - Placeholder formula needs real-world data (6-10 hours)

**MVP Timeline**: 1-2 weeks to completion (19-30 hours remaining)

**Next Steps**:
1. Relocate DS18B20 temp sensor (GPIO10 → GPIO12) and AirPump (GPIO11 → GPIO13)
2. Configure water level sensors on GPIO10-11 (XKC-Y23-V with voltage dividers)
3. Calibrate pH dosing formula
4. End-to-end testing

**Reference Documents**:
- `TODO.md` - Task list organized by phase (MVP → More Features → More Chambers)
- `.claude/plans/snazzy-yawning-rocket.md` - Detailed implementation plan

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

### 3-Layer HAL Architecture

PlantOS uses a 3-layer HAL (Hardware Abstraction Layer) architecture for clean separation of concerns and hardware independence:

```
┌─────────────────────────────────────────────────────────────┐
│ Layer 1: UNIFIED CONTROLLER (The Brain)                    │
│ - Single unified FSM orchestrating all system behavior     │
│ - States: INIT, IDLE, MAINTENANCE, ERROR, PH_*, FEEDING,   │
│   WATER_FILLING, WATER_EMPTYING                            │
│ - LED behavior system for visual feedback                  │
│ - Owns services: CentralStatusLogger                       │
│ - Uses services: PSM, Calendar (via dependency injection)  │
└────────────────┬────────────────────────────────────────────┘
                 │ (Controller → SafetyGate)
                 ▼
┌─────────────────────────────────────────────────────────────┐
│ Layer 2: ACTUATOR SAFETY GATE (The Guard)                  │
│ - Validates all actuator commands before execution         │
│ - Debouncing, duration limits, soft-start/soft-stop        │
│ - Runtime tracking with comprehensive violation logging    │
└────────────────┬────────────────────────────────────────────┘
                 │ (SafetyGate → HAL)
                 ▼
┌─────────────────────────────────────────────────────────────┐
│ Layer 3: HAL - Hardware Abstraction Layer (The Hands)      │
│ - Pure hardware interface: setPump(), readPH(), setLED()   │
│ - Wraps ESPHome components (Light, Sensor, GPIO, PWM)      │
│ - NO direct GPIO/I2C access above this layer               │
└─────────────────────────────────────────────────────────────┘
```

**Key Benefits:**
- **Hardware Independence**: Controller and SafetyGate work with any HAL implementation
- **Testability**: HAL can be mocked for unit testing without hardware
- **Clear Responsibility**: Each layer has a single, well-defined purpose
- **Safety by Design**: All hardware access flows through validation layers

### Configuration Files

- `plantOS.yaml`: Main ESPHome configuration (942 lines) defining hardware, components, and their interconnections
- `secrets.yaml`: WiFi credentials and other secrets (gitignored, see `secrets.example.yaml`)
- `flake.nix`: Nix development environment and build configuration
- `Taskfile.yml`: Task automation definitions

### Data Flow Overview

**Sensor Reading Path:**
```
UART (EZO pH Sensor at 115200 baud)
  → Sensor Filter (outlier rejection)
    → HAL (readPH with temperature compensation)
      → Unified Controller (state machine decisions)
```

**Actuator Control Path:**
```
Unified Controller (state handler)
  → Actuator Safety Gate (validate command)
    → HAL (setPump/setValve)
      → Hardware (pumps, valves, etc.)
```

**Service Integration:**
```
Unified Controller
  ├─ Calendar Manager (get daily schedule: pH targets, nutrient doses)
  ├─ Persistent State Manager (log critical events for crash recovery)
  └─ CentralStatusLogger (owned: system status, alerts, monitoring)
```

### Custom Components

Components are located in `components/` and follow ESPHome's external component structure. Each component has:
- Python code (`__init__.py`, `*.py`): ESPHome code generation and YAML config schema
- C++ code (`*.h`, `*.cpp`): Runtime implementation for ESP32

## Component Reference

### Layer 1: Unified Controller

#### plantos_controller

**Purpose**: Unified finite state machine orchestrating all system behavior
**Location**: `components/plantos_controller/`
**LOC**: ~1200

**Files:**
- `__init__.py`: ESPHome integration with dependency injection
- `controller.h/cpp`: Main FSM logic, state handlers, public API
- `led_behavior.h/cpp`: LED behavior system and manager
- `led_behaviors/*.h/.cpp`: Individual LED behaviors per state
- `CentralStatusLogger.h/cpp`: Owned status reporting system

**Architecture:**
- Enum-based FSM with switch statement dispatch
- 12 states covering all system operations
- Non-blocking timing with `millis()`
- Runs at ~1000 Hz in main loop
- LED behavior system for smooth animations

**State Diagram:**
```
INIT (3s) → IDLE (breathing green)
             ↓
             ├─ PH_MEASURING (5 min) → PH_CALCULATING → PH_INJECTING → PH_MIXING → (loop)
             ├─ FEEDING (sequential nutrient pumps)
             ├─ WATER_FILLING (30s)
             ├─ WATER_EMPTYING (30s)
             ├─ MAINTENANCE (persistent shutdown)
             └─ ERROR (5s timeout) → INIT
```

**States:**
```cpp
enum class ControllerState {
    INIT,              // Boot sequence (Red→Yellow→Green)
    IDLE,              // Ready state (Breathing Green)
    MAINTENANCE,       // Manual maintenance mode (Solid Yellow)
    ERROR,             // Error condition (Fast Red Flash)
    PH_MEASURING,      // pH stabilization phase (Yellow Pulse)
    PH_CALCULATING,    // Determining pH adjustment (Yellow Fast Blink)
    PH_INJECTING,      // Acid dosing in progress (Cyan Pulse)
    PH_MIXING,         // Air pump mixing after injection (Blue Pulse)
    PH_CALIBRATING,    // pH sensor calibration (Yellow Fast Blink)
    FEEDING,           // Nutrient dosing (Orange Pulse)
    WATER_FILLING,     // Fresh water addition (Blue Solid)
    WATER_EMPTYING     // Wastewater removal (Purple Pulse)
};
```

**Dependencies (Dependency Injection):**
- `plantos_hal::HAL*` - Required: Hardware abstraction layer
- `actuator_safety_gate::ActuatorSafetyGate*` - Required: Safety validation
- `persistent_state_manager::PersistentStateManager*` - Optional: Crash recovery
- `calendar_manager::CalendarManager*` - Optional: Grow schedule (future)

**Public API:**
```cpp
void startPhCorrection();        // Begin pH correction sequence
void startFeeding();             // Begin nutrient dosing sequence
void startFillTank();            // Begin water fill operation
void startEmptyTank();           // Begin water empty operation
bool toggleMaintenanceMode(bool enable);  // Enter/exit maintenance
void resetToInit();              // Reset FSM to INIT state
ControllerState getCurrentState();        // Get current state
CentralStatusLogger* getStatusLogger();   // Access status logger
```

**Usage:**
```yaml
plantos_controller:
  id: unified_controller
  hal: hal                      # Hardware abstraction layer
  safety_gate: actuator_safety  # Safety validation layer
  persistence: psm              # Optional: crash recovery
```

#### central_status_logger

**Purpose**: Unified logging and monitoring system
**Location**: `components/plantos_controller/` (owned by controller)
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

### Layer 2: Safety Gate

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

### Layer 3: Hardware Abstraction Layer (HAL)

#### plantos_hal

**Purpose**: Pure hardware abstraction interface for hardware independence
**Location**: `components/plantos_hal/`
**LOC**: ~300

**Architecture:**
- Abstract base class `HAL` defining hardware interface
- Concrete implementation `ESPHomeHAL` wrapping ESPHome components
- Enables testing via HAL mocking (no hardware required)
- Single point of hardware access for entire system

**HAL Interface:**
```cpp
class HAL {
public:
    virtual ~HAL() = default;

    // Actuators (called by SafetyGate)
    virtual void setPump(const std::string& pumpId, bool state) = 0;
    virtual void setValve(const std::string& valveId, bool state) = 0;
    virtual bool getPumpState(const std::string& pumpId) const = 0;

    // Sensors (called by Controller)
    virtual float readPH() = 0;
    virtual bool hasPhValue() const = 0;
    virtual void onPhChange(std::function<void(float)> callback) = 0;
    virtual bool startPhCalibration(float point) = 0;

    // User feedback (called by LED behaviors)
    virtual void setSystemLED(float r, float g, float b, float brightness = 1.0f) = 0;
    virtual void turnOffLED() = 0;
    virtual bool isLEDOn() const = 0;

    // System
    virtual uint32_t getSystemTime() const = 0;  // millis()
};
```

**ESPHomeHAL Implementation:**
- Wraps `light::LightState` for RGB LED control
- Wraps `sensor::Sensor` for pH reading with state callbacks
- Tracks pump/valve states in memory
- Uses `esphome::millis()` for system time

**Usage:**
```yaml
plantos_hal:
  id: hal
  system_led: system_led       # ESPHome light component
  ph_sensor: filtered_ph_sensor  # ESPHome sensor component
```

**Dependencies:**
- `esphome::light::LightState` - RGB LED
- `esphome::sensor::Sensor` - pH sensor
- GPIO/PWM outputs added by SafetyGate (future)

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

#### ezo_ph_uart

**Purpose**: Production pH sensor (Atlas Scientific EZO circuit via UART)
**Location**: `components/ezo_ph_uart/`
**LOC**: ~400
**Status**: ✅ WORKING (commits 8009973, 7d9d358)

**UART Protocol**: ASCII text commands at 115200 baud
**Critical Timing**: 300ms delay after write before reading
**GPIO Pins**: TX=GPIO20, RX=GPIO21 (configurable)

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
  - platform: ezo_ph_uart
    id: ezo_ph_uart_component
    name: "Raw pH (UART)"
    tx_pin: GPIO20
    rx_pin: GPIO21
    update_interval: 60s
```

**Note**: Legacy I2C version (`components/ezo_ph/`) exists but is not used. UART version is preferred for reliability.

---

#### DS18B20 Temperature Sensor

**Purpose**: Water temperature monitoring for pH compensation
**Platform**: ESPHome built-in `dallas_temp`
**Status**: ✅ CONFIGURED
**GPIO**: GPIO10 (1-Wire bus)

**Features:**
- Waterproof sensor probe
- ±0.5°C accuracy
- Temperature compensation sent to pH sensor via HAL
- Critical for pH accuracy (pH varies ±0.003 per °C)

**Usage:**
```yaml
dallas:
  - pin: GPIO10

sensor:
  - platform: dallas_temp
    id: water_temperature
    name: "Water Temperature"
    accuracy_decimals: 1
```

---

#### Light Sensor (KY-046)

**Purpose**: Ambient light monitoring
**Platform**: ESPHome built-in ADC
**Status**: ✅ CONFIGURED
**GPIO**: GPIO0 (ADC1_CH0)

**Usage:**
```yaml
sensor:
  - platform: adc
    pin: GPIO0
    id: light_intensity_raw
    name: "Light Intensity"
    update_interval: 10s
    attenuation: auto
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

PlantOS implements a multi-layer safety design with defense-in-depth:

### Layer 1: Unified Controller (PlantOSController)
- State machine flow validation (only valid state transitions allowed)
- Sequence validation (pH correction, feeding, water management)
- pH range checking (software limits: 5.0-7.5, triggers ERROR state)
- Max attempt limits (pH correction limited to 5 attempts)
- Safety margins (200ms margins after pump operations)

### Layer 2: ActuatorSafetyGate
- Debouncing (prevents redundant commands)
- Duration limits (enforces max runtime per actuator)
- Soft-start/soft-stop (PWM ramping for inrush protection)
- Runtime tracking with comprehensive violation logging
- All hardware access flows through validation

### Layer 3: Hardware Abstraction Layer (HAL)
- Single point of hardware access (no GPIO bypassing)
- Clean interface prevents direct hardware manipulation
- Enables testing and validation without physical hardware

### Layer 4: PersistentStateManager
- Critical event logging to NVS (survives power loss)
- Power-loss recovery and crash detection
- Operation age validation (detect stuck operations)

### Layer 5: Hardware Watchdog (WDTManager)
- System hang detection via ESP-IDF TWDT
- Automatic reset after 10s timeout
- Hardware-enforced (cannot be disabled by software bugs)

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

### Why Enum-Based FSM (Unified Controller)

**Design Choice**: Use `enum class ControllerState` with switch statement dispatch

**Advantages**:
- Type-safe state representation (compile-time checking)
- Easy to visualize all possible states
- Clear state transitions in code (explicit `transitionTo()` calls)
- LED behavior mapping straightforward (state → behavior)
- Debugging friendly (can print state names easily)

**Implementation**:
```cpp
void PlantOSController::loop() {
    switch (current_state_) {
        case ControllerState::INIT: handleInit(); break;
        case ControllerState::IDLE: handleIdle(); break;
        case ControllerState::PH_MEASURING: handlePhMeasuring(); break;
        // ... 9 more states
    }
}
```

**Trade-offs**:
- Slightly less memory efficient than function pointers (negligible on ESP32)
- All states compiled into single switch statement (acceptable for 12 states)

### Why 3-Layer HAL Architecture

**Problem with Old Architecture**:
- Dual FSMs (Controller + PlantOSLogic) created confusion about responsibility
- Direct hardware access scattered across components (hard to test)
- No clear abstraction layer for hardware (couldn't mock for testing)
- Tight coupling between application logic and ESPHome specifics

**Solution: 3-Layer HAL Design**:

**Layer 1: Unified Controller**:
- Single FSM orchestrating ALL system behavior (12 states)
- Clear state transitions with LED feedback per state
- Business logic (pH correction, feeding, water management)
- Hardware-independent (works with any HAL implementation)

**Layer 2: Actuator Safety Gate**:
- Single point of validation for ALL actuator commands
- Enforces safety rules consistently across system
- Hardware-independent (delegates to HAL for execution)
- Easy to audit and test safety logic

**Layer 3: HAL (Hardware Abstraction Layer)**:
- Pure hardware interface (setPump, readPH, setLED)
- Wraps ESPHome components (Light, Sensor, GPIO)
- Can be mocked for unit testing without hardware
- Enables future hardware changes without touching controller

**Benefits**:
1. **Testability**: Mock HAL for unit testing controller logic
2. **Clarity**: Each layer has single, well-defined responsibility
3. **Safety**: All hardware access flows through validation layers
4. **Portability**: Controller code works on any platform with HAL implementation
5. **Maintainability**: Changes to hardware don't require controller changes

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

## Next Steps to MVP Completion

**Current Status**: 85% complete - only 3 critical blockers remaining

### Critical Blockers (19-30 hours to MVP)

#### 1. GPIO Actuator Configuration ✅ COMPLETE
**Status**: All 7 actuators configured and HAL wired up

**Completed Configuration**:
- ✅ 7 GPIO output components added to `plantOS.yaml`
- ✅ 7 switch components wrapping outputs
- ✅ HAL dependency injection complete (`hal.h` and `__init__.py`)
- ✅ All actuators controllable via web UI and SafetyGate

**Actuators** (as configured in plantOS.yaml):
- GPIO11: AirPump (mixing/aeration) - **To be moved to GPIO12-17 for water level sensors**
- GPIO18: WaterValve (fresh water solenoid)
- GPIO19: AcidPump (pH down dosing)
- GPIO20: NutrientPumpA (grow phase nutrients)
- GPIO21: NutrientPumpB (micronutrients)
- GPIO22: NutrientPumpC (bloom phase nutrients)
- GPIO23: WastewaterPump (drainage)

**Water Level Sensors** (planned for GPIO10-11):
- GPIO10: Water Level HIGH sensor (XKC-Y23-V) - **Requires moving DS18B20 temp sensor**
- GPIO11: Water Level LOW sensor (XKC-Y23-V) - **Requires moving AirPump actuator**

**Hardware Note**: Use external relay board with 12V/24V supply (ESP32 can't source enough current)

#### 2. Water Level Sensors (3-4 hours) ⚠️ CRITICAL
**Hardware**: 2x XKC-Y23-V 5V capacitive level sensors

**Required Changes**:
- Configure binary sensors on GPIO10 (high) and GPIO11 (low)
- Add voltage dividers (5V → 3.3V) or level shifters
- Update WATER_FILLING/EMPTYING handlers to check levels and abort
- Add level status to CentralStatusLogger

**Safety**: Prevent overflow (abort fill on HIGH) and dry pump (abort empty on LOW)

**Note**: This reassigns GPIO10 (currently DS18B20 temp sensor) and GPIO11 (currently air pump). Temperature sensor and air pump will need to be moved to available GPIO12-17 range.

#### 3. pH Dosing Calibration (6-10 hours)
**Current**: Placeholder formula `duration_ms = ph_diff * 10.0f * 1000.0f`

**Required**:
- Measure reservoir volume and acid concentration
- Run 5+ calibration tests at different pH levels
- Create dosing formula (lookup table or regression)
- Update `calculate_acid_duration()` in `controller.cpp`
- Validate accuracy (±0.1 pH)
- Document in `CALIBRATION.md`

### MVP Success Criteria

Once the 3 blockers above are complete:

- [ ] All 7 actuators respond to web UI
- [ ] pH correction runs end-to-end automatically
- [ ] Feeding runs with all 3 nutrient pumps
- [ ] Water fill/empty aborts safely on level sensors
- [ ] Temperature compensation works
- [ ] PSM recovers after power loss
- [ ] SafetyGate enforces duration limits
- [ ] 24-hour unattended operation
- [ ] 48-hour pH stability (5.5-6.5)

### After MVP: Phase 2 (More Features)

See `TODO.md` for Phase 2 and Phase 3 roadmaps:

**Phase 2 - More Features** (40-68 hours):
- 120-day grow schedule
- Automated triggers (time/sensor-based)
- Light control (sunrise/sunset simulation)
- Air pump scheduling
- CSV data logging
- Cloud upload (MQTT/InfluxDB)
- TDS/EC sensor integration

**Phase 3 - More Chambers** (50-78 hours):
- Dual controller architecture
- Second chamber hardware (14 actuators + sensors)
- Independent schedules (main: flowering, mother: vegetative)
- Web UI for multi-chamber control

**Reference Documents**:
- `TODO.md` - Complete task list with 3-phase organization
- `.claude/plans/snazzy-yawning-rocket.md` - Detailed implementation plan with effort estimates

---

**Last Updated**: 2025-12-23
**Project Version**: 0.9 (MVP Finalization in Progress)
