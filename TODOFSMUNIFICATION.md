# FSM Unification Roadmap: PlantOS Controller + HAL Architecture

**Goal**: Merge dual FSM architecture (Controller + PlantOSLogic) into unified Controller with 3-layer HAL architecture.

**Status**: Planning Complete ✓ | Implementation Pending

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│ Layer 1: UNIFIED CONTROLLER (The Brain)                    │
│ - Orchestrates system behavior                             │
│ - Owns services: StatusLogger, PSM, Calendar               │
│ - States: INIT, IDLE, MAINTENANCE, ERROR, PH_*, FEEDING, WATER_* │
└────────────────┬────────────────────────────────────────────┘
                 │
                 ▼ (Controller → SafetyGate)
┌─────────────────────────────────────────────────────────────┐
│ Layer 2: ACTUATOR SAFETY GATE (The Guard)                  │
│ - Validates commands: debouncing, duration limits          │
│ - Calls HAL for execution                                  │
└────────────────┬────────────────────────────────────────────┘
                 │
                 ▼ (SafetyGate → HAL)
┌─────────────────────────────────────────────────────────────┐
│ Layer 3: HAL - Hardware Abstraction Layer (The Hands)      │
│ - Pure hardware interface: setPump(), readPH(), setSystemLED() │
│ - No direct GPIO/I2C above this layer                      │
└─────────────────────────────────────────────────────────────┘
```

---

## Phase 1: Create HAL (Layer 3)

### Issue #1.1: Define HAL Interface
**File**: `components/plantos_hal/hal.h`
**Priority**: P0 (Foundation)

**Task**: Create base HAL interface with all hardware methods

**Implementation Hints**:
```cpp
namespace plantos_hal {

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

} // namespace plantos_hal
```

**Dependencies**: None
**Testing**: Compile-only test

**Completion Status**: ✅ Done
- Created `components/plantos_hal/hal.h` with complete HAL interface
- Includes all actuator, sensor, LED, and system methods
- Added ESPHomeHAL class declaration with proper ESPHome integration

---

### Issue #1.2: Implement ESPHome HAL
**File**: `components/plantos_hal/hal.cpp`
**Priority**: P0

**Task**: Create ESPHomeHAL class wrapping ESPHome APIs

**Implementation Hints**:
```cpp
class ESPHomeHAL : public HAL {
public:
    ESPHomeHAL(
        esphome::light::LightState* led,
        esphome::sensor::Sensor* ph_sensor
        // Add GPIO/PWM outputs for pumps/valves
    );

    // Actuators - Wrap GPIO/PWM (stub for now, will connect to SafetyGate)
    void setPump(const std::string& pumpId, bool state) override {
        ESP_LOGI(TAG, "HAL::setPump(%s, %d)", pumpId.c_str(), state);
        // TODO: Actual GPIO control after SafetyGate integration
    }

    // Sensors - Wrap sensor::Sensor
    float readPH() override {
        return ph_sensor_ && ph_sensor_->has_state() ? ph_sensor_->state : 0.0f;
    }

    void onPhChange(std::function<void(float)> callback) override {
        if (ph_sensor_) {
            ph_sensor_->add_on_state_callback(callback);
        }
    }

    // LED - Wrap light::LightState
    void setSystemLED(float r, float g, float b, float brightness) override {
        if (!led_) return;
        auto call = led_->make_call();
        call.set_state(brightness > 0.01f);
        call.set_brightness(brightness);
        call.set_rgb(r, g, b);
        call.perform();
    }

    // System
    uint32_t getSystemTime() const override {
        return millis();
    }

private:
    static constexpr const char* TAG = "plantos_hal";
    esphome::light::LightState* led_;
    esphome::sensor::Sensor* ph_sensor_;
};
```

**Dependencies**: Issue #1.1
**Testing**: Manual test - verify LED control, pH reading

**Completion Status**: ✅ Done
- Created `components/plantos_hal/hal.cpp` with full ESPHomeHAL implementation
- Wrapped LED control (light::LightState) with setSystemLED, turnOffLED, isLEDOn
- Wrapped pH sensor (sensor::Sensor) with readPH, hasPhValue, onPhChange
- Implemented pump/valve state tracking (stubs for actual GPIO in Phase 2)
- Used esphome::millis() for system time
- All methods compile successfully

---

### Issue #1.3: ESPHome Python Integration for HAL
**File**: `components/plantos_hal/__init__.py`
**Priority**: P0

**Task**: Create Python schema and code generation for HAL

**Implementation Hints**:
```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, sensor

plantos_hal_ns = cg.esphome_ns.namespace('plantos_hal')
HAL = plantos_hal_ns.class_('HAL')
ESPHomeHAL = plantos_hal_ns.class_('ESPHomeHAL', HAL)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ESPHomeHAL),
    cv.Required('system_led'): cv.use_id(light.LightState),
    cv.Required('ph_sensor'): cv.use_id(sensor.Sensor),
    # Add GPIO/PWM outputs later
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    led = await cg.get_variable(config['system_led'])
    cg.add(var.set_led(led))

    ph = await cg.get_variable(config['ph_sensor'])
    cg.add(var.set_ph_sensor(ph))
```

**Dependencies**: Issue #1.2
**Testing**: YAML parse test

**Completion Status**: ✅ Done
- Created `components/plantos_hal/__init__.py` with ESPHome Python integration
- Defined plantos_hal namespace and ESPHomeHAL class for code generation
- Created CONFIG_SCHEMA with system_led and ph_sensor dependencies
- Implemented to_code() function for dependency injection
- Successfully integrated with ESPHome build system
- Full compilation successful with HAL component

---

## Phase 2: Update SafetyGate (Layer 2)

### Issue #2.1: Add HAL Dependency to SafetyGate
**File**: `components/actuator_safety_gate/ActuatorSafetyGate.h`
**Priority**: P0

**Task**: Modify SafetyGate to receive and use HAL

**Implementation Hints**:
```cpp
class ActuatorSafetyGate : public Component {
public:
    // Add HAL setter
    void setHAL(plantos_hal::HAL* hal) { hal_ = hal; }

    bool executeCommand(const std::string& actuatorId, bool state, uint32_t duration);

private:
    plantos_hal::HAL* hal_{nullptr};

    // Update turnOnActuator/turnOffActuator to use HAL
    void turnOnActuator(const std::string& actuatorId);
    void turnOffActuator(const std::string& actuatorId);
};
```

**Current Code** (`ActuatorSafetyGate.cpp`):
```cpp
// OLD: Direct GPIO control
void ActuatorSafetyGate::turnOnActuator(const std::string& actuatorId) {
    // Direct GPIO pin writes
}

// NEW: HAL-based control
void ActuatorSafetyGate::turnOnActuator(const std::string& actuatorId) {
    if (!hal_) {
        ESP_LOGE(TAG, "HAL not set!");
        return;
    }

    if (actuatorId.find("Pump") != std::string::npos) {
        hal_->setPump(actuatorId, true);
    } else if (actuatorId.find("Valve") != std::string::npos) {
        hal_->setValve(actuatorId, true);
    }
}
```

**Dependencies**: Phase 1 complete
**Testing**: Verify SafetyGate calls HAL correctly

**Completion Status**: ✅ Done
- Added forward declaration for plantos_hal::HAL in ActuatorSafetyGate.h
- Added setHAL() method for dependency injection
- Added private hal_ member variable
- Created executeHardwareCommand() helper method to route commands to HAL
- Updated executeCommand() to call executeHardwareCommand() for instant ON/OFF
- Updated updateRamping() to call executeHardwareCommand() when ramp completes
- Actuators are identified by name pattern (Pump/Valve) and routed to correct HAL method
- Compilation successful with HAL integration

---

### Issue #2.2: Update SafetyGate Python Integration
**File**: `components/actuator_safety_gate/__init__.py`
**Priority**: P0

**Task**: Inject HAL dependency into SafetyGate

**Implementation Hints**:
```python
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ActuatorSafetyGate),
    cv.Required('hal'): cv.use_id(plantos_hal.HAL),  # NEW
    cv.Optional('acid_pump_max_duration', default=30): cv.positive_int,
    # ... existing config
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Inject HAL
    hal = await cg.get_variable(config['hal'])
    cg.add(var.setHAL(hal))

    # ... existing config
```

**Dependencies**: Issue #2.1
**Testing**: YAML parse test, verify HAL injection

**Completion Status**: ✅ Done
- Updated __init__.py to import plantos_hal namespace and HAL class
- Added CONF_HAL configuration key
- Updated CONFIG_SCHEMA to require 'hal' parameter
- Updated to_code() to inject HAL dependency via var.setHAL(hal)
- Updated plantOS.yaml to add hal: hal to actuator_safety_gate configuration
- Full compilation successful with HAL dependency injection

---

## Phase 3: Create LED Behavior System

### Issue #3.1: LED Behavior Base Classes
**File**: `components/plantos_controller/led_behaviors/led_behavior.h`
**Priority**: P1

**Task**: Define LED behavior interface and manager

**Implementation Hints**:
```cpp
namespace plantos_controller {

class LedBehavior {
public:
    virtual ~LedBehavior() = default;
    virtual void onEnter() {}
    virtual void update(plantos_hal::HAL* hal, uint32_t elapsed) = 0;
    virtual void onExit() {}
    virtual bool isComplete(uint32_t elapsed) const { return false; }
};

class LedBehaviorSystem {
public:
    void update(ControllerState state, uint32_t stateElapsed, plantos_hal::HAL* hal);
    void setBehavior(ControllerState state, std::unique_ptr<LedBehavior> behavior);

private:
    ControllerState current_state_{ControllerState::INIT};
    std::unique_ptr<LedBehavior> current_behavior_;
    std::map<ControllerState, std::unique_ptr<LedBehavior>> behaviors_;
};
```

**Dependencies**: Phase 1 complete
**Testing**: Compile-only test

---

### Issue #3.2: Implement Boot Sequence Behavior
**File**: `components/plantos_controller/led_behaviors/boot_sequence.cpp`
**Priority**: P1

**Task**: Red → Yellow → Green boot sequence (3 seconds)

**Implementation Hints**:
```cpp
class BootSequenceBehavior : public LedBehavior {
public:
    void update(plantos_hal::HAL* hal, uint32_t elapsed) override {
        if (elapsed < 1000) {
            hal->setSystemLED(1.0f, 0.0f, 0.0f);  // Red
        } else if (elapsed < 2000) {
            hal->setSystemLED(1.0f, 1.0f, 0.0f);  // Yellow
        } else {
            hal->setSystemLED(0.0f, 1.0f, 0.0f);  // Green
        }
    }

    bool isComplete(uint32_t elapsed) const override {
        return elapsed >= 3000;
    }
};
```

**Dependencies**: Issue #3.1
**Testing**: Visual test - verify LED shows R→Y→G sequence

---

### Issue #3.3: Implement Breathing Green Behavior
**File**: `components/plantos_controller/led_behaviors/breathing_green.cpp`
**Priority**: P1

**Task**: Sine wave breathing animation for IDLE state

**Implementation Hints**:
```cpp
class BreathingGreenBehavior : public LedBehavior {
public:
    void update(plantos_hal::HAL* hal, uint32_t elapsed) override {
        float t = elapsed / 1000.0f;
        float brightness = (std::sin(t * 3.14159f) + 1.0f) / 2.0f;
        brightness = 0.1f + (brightness * 0.9f);  // 10% to 100%
        hal->setSystemLED(0.0f, 1.0f, 0.0f, brightness);
    }
};
```

**Dependencies**: Issue #3.1
**Testing**: Visual test - verify smooth breathing

---

### Issue #3.4: Implement Other LED Behaviors
**Files**:
- `yellow_pulse.cpp` (PH_MEASURING)
- `yellow_fast_blink.cpp` (PH_CALCULATING)
- `cyan_pulse.cpp` (PH_INJECTING)
- `blue_pulse.cpp` (PH_MIXING)
- `orange_pulse.cpp` (FEEDING)
- `error_flash.cpp` (ERROR)
- `solid_yellow.cpp` (MAINTENANCE)

**Priority**: P1

**Task**: Implement remaining LED behaviors for all Controller states

**Implementation Hints**:
```cpp
// Yellow Pulse (PH_MEASURING)
class YellowPulseBehavior : public LedBehavior {
    void update(plantos_hal::HAL* hal, uint32_t elapsed) override {
        float brightness = (std::sin((elapsed / 1000.0f) * 3.14159f) + 1.0f) / 2.0f;
        brightness = 0.3f + (brightness * 0.7f);
        hal->setSystemLED(1.0f, 1.0f, 0.0f, brightness);  // Yellow
    }
};

// Error Flash (ERROR)
class ErrorFlashBehavior : public LedBehavior {
    void update(plantos_hal::HAL* hal, uint32_t elapsed) override {
        bool on = (elapsed / 100) % 2 == 0;  // 5 Hz flash
        if (on) {
            hal->setSystemLED(1.0f, 0.0f, 0.0f);  // Red
        } else {
            hal->turnOffLED();
        }
    }
};
```

**Dependencies**: Issue #3.1
**Testing**: Visual test for each behavior

---

## Phase 4: Create Unified Controller (Layer 1)

### Issue #4.1: Define Controller Class
**File**: `components/plantos_controller/controller.h`
**Priority**: P0

**Task**: Create unified Controller with dependency injection

**Implementation Hints**:
```cpp
namespace plantos_controller {

enum class ControllerState {
    INIT, IDLE, MAINTENANCE, ERROR,
    PH_MEASURING, PH_CALCULATING, PH_INJECTING, PH_MIXING, PH_CALIBRATING,
    FEEDING, WATER_FILLING, WATER_EMPTYING
};

class PlantOSController : public esphome::Component {
public:
    void setup() override;
    void loop() override;

    // Dependency Injection
    void setHAL(plantos_hal::HAL* hal) { hal_ = hal; }
    void setSafetyGate(actuator_safety_gate::ActuatorSafetyGate* gate) { safety_gate_ = gate; }
    void setStatusLogger(central_status_logger::CentralStatusLogger* logger) { status_logger_ = logger; }
    void setPersistenceManager(persistent_state_manager::PersistentStateManager* psm) { psm_ = psm; }
    void setCalendar(calendar_manager::CalendarManager* calendar) { calendar_ = calendar; }

    // Public API
    void startPhCorrection();
    void startFeeding();
    void startFillTank();
    void startEmptyTank();
    bool toggleMaintenanceMode(bool state);
    void resetToInit();

private:
    // Dependencies
    plantos_hal::HAL* hal_{nullptr};
    actuator_safety_gate::ActuatorSafetyGate* safety_gate_{nullptr};
    central_status_logger::CentralStatusLogger* status_logger_{nullptr};
    persistent_state_manager::PersistentStateManager* psm_{nullptr};
    calendar_manager::CalendarManager* calendar_{nullptr};

    // FSM state
    ControllerState current_state_{ControllerState::INIT};
    uint32_t state_start_time_{0};
    uint32_t state_counter_{0};

    // LED behaviors
    std::unique_ptr<LedBehaviorSystem> led_behaviors_;

    // State handlers
    void handleInit();
    void handleIdle();
    void handleMaintenance();
    void handleError();
    void handlePhMeasuring();
    void handlePhCalculating();
    void handlePhInjecting();
    void handlePhMixing();
    void handleFeeding();
    void handleWaterFilling();
    void handleWaterEmptying();

    void transitionTo(ControllerState newState);

    // Actuator helpers (Controller → SafetyGate → HAL)
    bool requestPump(const std::string& pumpId, bool state, uint32_t duration = 0);
    bool requestValve(const std::string& valveId, bool state, uint32_t duration = 0);
    void turnOffAllPumps();

    static constexpr const char* TAG = "plantos_controller";
};

} // namespace plantos_controller
```

**Dependencies**: Phase 2, Phase 3
**Testing**: Compile-only test

---

### Issue #4.2: Implement Controller Loop
**File**: `components/plantos_controller/controller.cpp`
**Priority**: P0

**Task**: Implement main FSM driver loop

**Implementation Hints**:
```cpp
void PlantOSController::loop() {
    if (!hal_ || !safety_gate_ || !status_logger_) {
        return;  // Dependencies not injected yet
    }

    // Update LED behaviors (runs every loop for smooth animations)
    uint32_t stateElapsed = hal_->getSystemTime() - state_start_time_;
    led_behaviors_->update(current_state_, stateElapsed, hal_);

    // Priority: Handle maintenance mode transitions
    if (shutdown_requested_ && current_state_ == ControllerState::IDLE) {
        transitionTo(ControllerState::MAINTENANCE);
    }
    if (!shutdown_requested_ && current_state_ == ControllerState::MAINTENANCE) {
        transitionTo(ControllerState::IDLE);
    }

    // Execute current state handler
    switch (current_state_) {
        case ControllerState::INIT:
            handleInit();
            break;
        case ControllerState::IDLE:
            handleIdle();
            break;
        case ControllerState::PH_MEASURING:
            handlePhMeasuring();
            break;
        case ControllerState::PH_CALCULATING:
            handlePhCalculating();
            break;
        case ControllerState::PH_INJECTING:
            handlePhInjecting();
            break;
        case ControllerState::PH_MIXING:
            handlePhMixing();
            break;
        case ControllerState::FEEDING:
            handleFeeding();
            break;
        case ControllerState::WATER_FILLING:
            handleWaterFilling();
            break;
        case ControllerState::WATER_EMPTYING:
            handleWaterEmptying();
            break;
        case ControllerState::MAINTENANCE:
            handleMaintenance();
            break;
        case ControllerState::ERROR:
            handleError();
            break;
    }
}

void PlantOSController::transitionTo(ControllerState newState) {
    if (newState == current_state_) return;

    ESP_LOGI(TAG, "State transition: %s -> %s",
             getStateName(current_state_), getStateName(newState));

    current_state_ = newState;
    state_start_time_ = hal_->getSystemTime();
    state_counter_ = 0;

    // Update status logger
    if (status_logger_) {
        status_logger_->updateLogicState(getStateName(newState));
    }
}

// Actuator helper
bool PlantOSController::requestPump(const std::string& pumpId, bool state, uint32_t duration) {
    if (!safety_gate_) return false;
    return safety_gate_->executeCommand(pumpId, state, duration);
}
```

**Dependencies**: Issue #4.1
**Testing**: Verify loop runs at ~1000 Hz

---

### Issue #4.3: Implement INIT State Handler
**File**: `components/plantos_controller/states/state_init.cpp`
**Priority**: P1

**Task**: Boot sequence state (3 seconds) with R→Y→G LED

**Implementation Hints**:
```cpp
void PlantOSController::handleInit() {
    uint32_t elapsed = hal_->getSystemTime() - state_start_time_;

    // Boot sequence LED handled by LedBehaviorSystem
    // Just wait for completion
    if (elapsed >= 3000) {
        ESP_LOGI(TAG, "Boot sequence complete");
        transitionTo(ControllerState::IDLE);
    }
}
```

**Current Reference**: `components/controller/state_init.cpp`
**Dependencies**: Issue #4.2, Phase 3
**Testing**: Verify LED shows R→Y→G, then transitions to IDLE

---

### Issue #4.4: Implement IDLE State Handler
**File**: `components/plantos_controller/states/state_idle.cpp`
**Priority**: P1

**Task**: Idle state with breathing green LED, waits for manual triggers

**Implementation Hints**:
```cpp
void PlantOSController::handleIdle() {
    // Breathing green LED handled by LedBehaviorSystem

    // Check for sensor critical conditions (once per second)
    uint32_t elapsed = hal_->getSystemTime() - state_start_time_;
    uint32_t current_seconds = elapsed / 1000;

    if (current_seconds > state_counter_) {
        state_counter_ = current_seconds;

        // Monitor pH sensor
        if (hal_->hasPhValue()) {
            float ph = hal_->readPH();

            // Critical pH check
            if (ph < 5.0f || ph > 7.5f) {
                ESP_LOGE(TAG, "Critical pH detected: %.2f", ph);
                if (status_logger_) {
                    status_logger_->updateAlert("PH_CRITICAL", "pH out of safe range");
                }
                transitionTo(ControllerState::ERROR);
            }
        }
    }

    // State changes happen via public API methods (startPhCorrection, etc.)
}
```

**Current Reference**: `components/controller/state_ready.cpp`
**Dependencies**: Issue #4.3
**Testing**: Verify breathing animation, critical pH detection

---

### Issue #4.5: Controller Python Integration
**File**: `components/plantos_controller/__init__.py`
**Priority**: P0

**Task**: ESPHome schema and dependency injection

**Implementation Hints**:
```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import plantos_hal, actuator_safety_gate, central_status_logger, persistent_state_manager, calendar_manager

plantos_controller_ns = cg.esphome_ns.namespace('plantos_controller')
PlantOSController = plantos_controller_ns.class_('PlantOSController', cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(PlantOSController),
    cv.Required('hal'): cv.use_id(plantos_hal.HAL),
    cv.Required('safety_gate'): cv.use_id(actuator_safety_gate.ActuatorSafetyGate),
    cv.Required('status_logger'): cv.use_id(central_status_logger.CentralStatusLogger),
    cv.Required('persistence'): cv.use_id(persistent_state_manager.PersistentStateManager),
    cv.Required('calendar'): cv.use_id(calendar_manager.CalendarManager),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Layer 3: HAL
    hal = await cg.get_variable(config['hal'])
    cg.add(var.setHAL(hal))

    # Layer 2: SafetyGate
    gate = await cg.get_variable(config['safety_gate'])
    cg.add(var.setSafetyGate(gate))

    # Layer 1: Services
    logger = await cg.get_variable(config['status_logger'])
    cg.add(var.setStatusLogger(logger))

    psm = await cg.get_variable(config['persistence'])
    cg.add(var.setPersistenceManager(psm))

    calendar = await cg.get_variable(config['calendar'])
    cg.add(var.setCalendar(calendar))
```

**Dependencies**: Issue #4.1
**Testing**: YAML parse test, verify all dependencies injected

---

## Phase 5: Port pH Correction States

### Issue #5.1: Implement PH_MEASURING State
**File**: `components/plantos_controller/states/state_ph_routine.cpp`
**Priority**: P2

**Task**: 5-minute stabilization with all pumps OFF

**Implementation Hints**:
```cpp
void PlantOSController::handlePhMeasuring() {
    uint32_t elapsed = hal_->getSystemTime() - state_start_time_;

    // Turn off all pumps for stabilization
    if (elapsed < 100) {
        turnOffAllPumps();
        ESP_LOGI(TAG, "pH measuring: All pumps OFF for stabilization");
        return;
    }

    // Take readings every minute
    uint32_t reading_interval = elapsed / 60000;  // Every 60 seconds
    if (reading_interval > state_counter_) {
        state_counter_ = reading_interval;

        if (hal_->hasPhValue()) {
            float ph = hal_->readPH();
            ph_readings_.push_back(ph);
            ESP_LOGI(TAG, "pH reading #%d: %.2f", ph_readings_.size(), ph);

            // Check for critical pH
            if (ph < 5.0f || ph > 7.5f) {
                if (status_logger_) {
                    status_logger_->updateAlert("PH_CRITICAL", "pH out of safe range");
                }
            }
        }
    }

    // Wait 5 minutes
    if (elapsed < 300000) return;  // 5 minutes = 300000ms

    // Calculate robust average and transition
    ph_current_ = calculateRobustPhAverage();
    ESP_LOGI(TAG, "pH stabilization complete. Average: %.2f", ph_current_);
    transitionTo(ControllerState::PH_CALCULATING);
}
```

**Current Reference**: `components/plantos_logic/PlantOSLogic.cpp:319-377` (handle_ph_measuring)
**Dependencies**: Issue #4.4
**Testing**: Verify 5min timing, readings collected, pumps OFF

---

### Issue #5.2: Implement PH_CALCULATING State
**File**: `components/plantos_controller/states/state_ph_routine.cpp`
**Priority**: P2

**Task**: Determine if pH correction needed, calculate dose

**Implementation Hints**:
```cpp
void PlantOSController::handlePhCalculating() {
    // Get target pH from Calendar
    if (!calendar_) {
        ESP_LOGE(TAG, "Calendar not available!");
        transitionTo(ControllerState::IDLE);
        return;
    }

    auto schedule = calendar_->getTodaySchedule();
    float target_min = schedule.target_ph_min;
    float target_max = schedule.target_ph_max;

    ESP_LOGI(TAG, "pH: %.2f, Target: %.2f-%.2f", ph_current_, target_min, target_max);

    // Check if in range
    if (ph_current_ >= target_min && ph_current_ <= target_max) {
        ESP_LOGI(TAG, "pH within target range, no correction needed");
        if (psm_) psm_->clearEvent();
        if (status_logger_) status_logger_->clearAlert("PH_CRITICAL");
        transitionTo(ControllerState::IDLE);
        return;
    }

    // Calculate acid dose
    uint32_t dose_ms = calculateAcidDuration(ph_current_, target_max);

    // Check minimum dose threshold
    if (dose_ms < 1000) {
        ESP_LOGI(TAG, "Dose too small (<1s), skipping");
        transitionTo(ControllerState::IDLE);
        return;
    }

    // Check max attempts
    if (ph_attempt_count_ >= 5) {
        ESP_LOGE(TAG, "Max pH correction attempts reached (5)");
        if (status_logger_) {
            status_logger_->updateAlert("PH_MAX_ATTEMPTS", "Failed to correct pH after 5 attempts");
        }
        transitionTo(ControllerState::IDLE);
        return;
    }

    // Proceed with injection
    ph_dose_duration_ms_ = dose_ms;
    ph_attempt_count_++;
    ESP_LOGI(TAG, "pH correction needed: %.2f -> %.2f (dose: %dms, attempt %d/5)",
             ph_current_, target_max, dose_ms, ph_attempt_count_);
    transitionTo(ControllerState::PH_INJECTING);
}
```

**Current Reference**: `components/plantos_logic/PlantOSLogic.cpp:379-462` (handle_ph_calculating)
**Dependencies**: Issue #5.1
**Testing**: Verify decision logic, dose calculation

---

### Issue #5.3: Implement PH_INJECTING State
**File**: `components/plantos_controller/states/state_ph_routine.cpp`
**Priority**: P2

**Task**: Activate acid pump + air pump for dosing

**Implementation Hints**:
```cpp
void PlantOSController::handlePhInjecting() {
    uint32_t elapsed = hal_->getSystemTime() - state_start_time_;

    // On entry: Activate pumps
    if (elapsed < 100) {
        // Air pump ON for mixing
        requestPump("AirPump", true, 0);  // Continuous

        // Acid pump ON for calculated duration
        uint32_t dose_sec = (ph_dose_duration_ms_ + 999) / 1000;  // Round up
        bool approved = requestPump("AcidPump", true, dose_sec);

        if (!approved) {
            ESP_LOGE(TAG, "Acid pump rejected by SafetyGate!");
            transitionTo(ControllerState::ERROR);
            return;
        }

        ESP_LOGI(TAG, "Dosing acid: %dms (Air pump mixing)", ph_dose_duration_ms_);
    }

    // Wait for dose + safety margin
    uint32_t total_duration = ph_dose_duration_ms_ + 200;
    if (elapsed < total_duration) return;

    // Explicit OFF (safety)
    requestPump("AcidPump", false, 0);

    ESP_LOGI(TAG, "Acid dosing complete, starting mixing");
    transitionTo(ControllerState::PH_MIXING);
}
```

**Current Reference**: `components/plantos_logic/PlantOSLogic.cpp:464-514` (handle_ph_injecting)
**Dependencies**: Issue #5.2
**Testing**: Verify acid pump activates, SafetyGate approves

---

### Issue #5.4: Implement PH_MIXING State
**File**: `components/plantos_controller/states/state_ph_routine.cpp`
**Priority**: P2

**Task**: Air pump ON for 2 minutes, then re-measure

**Implementation Hints**:
```cpp
void PlantOSController::handlePhMixing() {
    uint32_t elapsed = hal_->getSystemTime() - state_start_time_;

    // Keep air pump ON for 2 minutes
    if (elapsed < 120000) return;  // 2 minutes = 120000ms

    // Turn off air pump
    requestPump("AirPump", false, 0);

    ESP_LOGI(TAG, "Mixing complete, re-measuring pH");

    // Clear readings and re-measure
    ph_readings_.clear();
    transitionTo(ControllerState::PH_MEASURING);
}
```

**Current Reference**: `components/plantos_logic/PlantOSLogic.cpp:516-536` (handle_ph_mixing)
**Dependencies**: Issue #5.3
**Testing**: Verify 2min mixing, loop back to PH_MEASURING

---

### Issue #5.5: Add pH Correction Public API
**File**: `components/plantos_controller/controller.cpp`
**Priority**: P2

**Task**: Implement startPhCorrection() method

**Implementation Hints**:
```cpp
void PlantOSController::startPhCorrection() {
    if (current_state_ != ControllerState::IDLE) {
        ESP_LOGW(TAG, "Cannot start pH correction from state: %s", getStateName(current_state_));
        return;
    }

    ESP_LOGI(TAG, "Starting pH correction sequence");

    // Reset attempt counter and readings
    ph_attempt_count_ = 0;
    ph_readings_.clear();

    // Log event for persistence
    if (psm_) {
        psm_->logEvent("PH_CORRECTION", 0);  // 0 = STARTED
    }

    transitionTo(ControllerState::PH_MEASURING);
}
```

**Dependencies**: Issues #5.1-5.4
**Testing**: Manual trigger via button, verify full sequence

---

## Phase 6: Port Feeding and Water Management

### Issue #6.1: Implement FEEDING State
**File**: `components/plantos_controller/states/state_feeding.cpp`
**Priority**: P2

**Task**: Sequential nutrient pump activation based on Calendar schedule

**Implementation Hints**:
```cpp
void PlantOSController::handleFeeding() {
    // Get schedule from Calendar
    auto schedule = calendar_->getTodaySchedule();

    uint32_t elapsed = hal_->getSystemTime() - state_start_time_;

    // Sequential pump activation
    if (elapsed < schedule.nutrient_A_duration_ms) {
        // Pump A active
        if (state_counter_ == 0) {
            requestPump("NutrientPumpA", true, schedule.nutrient_A_duration_ms / 1000);
            state_counter_ = 1;
        }
        return;
    }

    if (elapsed < schedule.nutrient_A_duration_ms + schedule.nutrient_B_duration_ms) {
        // Pump B active
        if (state_counter_ == 1) {
            requestPump("NutrientPumpA", false, 0);  // Ensure A is OFF
            requestPump("NutrientPumpB", true, schedule.nutrient_B_duration_ms / 1000);
            state_counter_ = 2;
        }
        return;
    }

    if (elapsed < schedule.nutrient_A_duration_ms + schedule.nutrient_B_duration_ms + schedule.nutrient_C_duration_ms) {
        // Pump C active
        if (state_counter_ == 2) {
            requestPump("NutrientPumpB", false, 0);  // Ensure B is OFF
            requestPump("NutrientPumpC", true, schedule.nutrient_C_duration_ms / 1000);
            state_counter_ = 3;
        }
        return;
    }

    // All pumps complete
    requestPump("NutrientPumpC", false, 0);
    ESP_LOGI(TAG, "Feeding complete");

    if (psm_) psm_->clearEvent();
    transitionTo(ControllerState::IDLE);
}
```

**Current Reference**: `components/plantos_logic/PlantOSLogic.cpp:560-620` (handle_feeding)
**Dependencies**: Phase 5 complete
**Testing**: Verify sequential pump activation, timing from Calendar

---

### Issue #6.2: Implement WATER_FILLING State
**File**: `components/plantos_controller/states/state_water.cpp`
**Priority**: P2

**Task**: Open water valve for 30 seconds

**Implementation Hints**:
```cpp
void PlantOSController::handleWaterFilling() {
    uint32_t elapsed = hal_->getSystemTime() - state_start_time_;

    // Activate valve on entry
    if (elapsed < 100) {
        bool approved = requestValve("WaterValve", true, 30);  // 30 seconds
        if (!approved) {
            ESP_LOGE(TAG, "Water valve rejected by SafetyGate!");
            transitionTo(ControllerState::ERROR);
            return;
        }
        ESP_LOGI(TAG, "Water filling started (30s)");
    }

    // Wait 30 seconds
    if (elapsed < 30000) return;

    // Explicit close
    requestValve("WaterValve", false, 0);
    ESP_LOGI(TAG, "Water filling complete");

    if (psm_) psm_->clearEvent();
    transitionTo(ControllerState::IDLE);
}
```

**Current Reference**: `components/plantos_logic/PlantOSLogic.cpp:653-691` (handle_water_filling)
**Dependencies**: Phase 5 complete
**Testing**: Verify valve opens 30s, closes automatically

---

### Issue #6.3: Implement WATER_EMPTYING State
**File**: `components/plantos_controller/states/state_water.cpp`
**Priority**: P2

**Task**: Activate wastewater pump for 30 seconds

**Implementation Hints**:
```cpp
void PlantOSController::handleWaterEmptying() {
    uint32_t elapsed = hal_->getSystemTime() - state_start_time_;

    // Activate pump on entry
    if (elapsed < 100) {
        bool approved = requestPump("WastewaterPump", true, 30);
        if (!approved) {
            ESP_LOGE(TAG, "Wastewater pump rejected by SafetyGate!");
            transitionTo(ControllerState::ERROR);
            return;
        }
        ESP_LOGI(TAG, "Water emptying started (30s)");
    }

    // Wait 30 seconds
    if (elapsed < 30000) return;

    // Explicit OFF
    requestPump("WastewaterPump", false, 0);
    ESP_LOGI(TAG, "Water emptying complete");

    if (psm_) psm_->clearEvent();
    transitionTo(ControllerState::IDLE);
}
```

**Current Reference**: `components/plantos_logic/PlantOSLogic.cpp:693-730` (handle_water_emptying)
**Dependencies**: Phase 5 complete
**Testing**: Verify pump runs 30s, stops automatically

---

## Phase 7: Port Maintenance and Error States

### Issue #7.1: Implement MAINTENANCE State
**File**: `components/plantos_controller/states/state_maintenance.cpp`
**Priority**: P2

**Task**: Persistent shutdown state with solid yellow LED

**Implementation Hints**:
```cpp
void PlantOSController::handleMaintenance() {
    // Solid yellow LED handled by LedBehaviorSystem

    // Turn off all pumps on entry
    uint32_t elapsed = hal_->getSystemTime() - state_start_time_;
    if (elapsed < 100) {
        turnOffAllPumps();
        ESP_LOGI(TAG, "Maintenance mode: All pumps OFF");

        // Save persistent state
        if (psm_) {
            psm_->saveState("ShutdownState", true);
        }

        if (status_logger_) {
            status_logger_->updateMaintenanceMode(true);
        }
    }

    // Stay in this state until toggleMaintenanceMode(false) is called
}

bool PlantOSController::toggleMaintenanceMode(bool state) {
    if (state) {
        // Enter maintenance
        shutdown_requested_ = true;
        if (current_state_ == ControllerState::IDLE) {
            transitionTo(ControllerState::MAINTENANCE);
        }
    } else {
        // Exit maintenance
        shutdown_requested_ = false;
        if (psm_) {
            psm_->saveState("ShutdownState", false);
        }
        if (status_logger_) {
            status_logger_->updateMaintenanceMode(false);
        }
        if (current_state_ == ControllerState::MAINTENANCE) {
            transitionTo(ControllerState::IDLE);
        }
    }
    return true;
}
```

**Current Reference**: `components/plantos_logic/PlantOSLogic.cpp:894-975` (toggle_maintenance_mode)
**Dependencies**: Phase 6 complete
**Testing**: Verify persistent state, pumps OFF, transitions

---

### Issue #7.2: Implement ERROR State
**File**: `components/plantos_controller/states/state_error.cpp`
**Priority**: P2

**Task**: Fast red flashing for 5 seconds, then return to INIT

**Implementation Hints**:
```cpp
void PlantOSController::handleError() {
    uint32_t elapsed = hal_->getSystemTime() - state_start_time_;

    // Fast red flash handled by LedBehaviorSystem (5 Hz)

    // Turn off all pumps on entry
    if (elapsed < 100) {
        turnOffAllPumps();
        ESP_LOGE(TAG, "ERROR state: All pumps OFF");
    }

    // Wait 5 seconds
    if (elapsed < 5000) return;

    // Clear alerts and restart
    if (status_logger_) {
        status_logger_->clearAlert("PH_CRITICAL");
        status_logger_->clearAlert("SENSOR_CRITICAL");
    }

    ESP_LOGI(TAG, "ERROR state timeout, restarting to INIT");
    transitionTo(ControllerState::INIT);
}
```

**Current Reference**: `components/controller/state_error.cpp`
**Dependencies**: Phase 6 complete
**Testing**: Verify 5s timeout, LED flashing, return to INIT

---

## Phase 8: Migrate YAML Configuration

### Issue #8.1: Create New YAML Schema
**File**: `plantOS.yaml`
**Priority**: P0

**Task**: Replace old dual FSM config with 3-layer architecture

**Implementation Hints**:
```yaml
# ============================================================================
# Layer 3: HAL (Hardware Abstraction Layer)
# ============================================================================
plantos_hal:
  id: hal
  system_led: system_led
  ph_sensor: ph_sensor_real

# ============================================================================
# Layer 2: Actuator Safety Gate
# ============================================================================
actuator_safety_gate:
  id: safety_gate
  hal: hal  # NOW CALLS HAL INSTEAD OF GPIO
  acid_pump_max_duration: 30
  nutrient_pump_max_duration: 60
  water_valve_max_duration: 300
  wastewater_pump_max_duration: 180
  air_pump_max_duration: 600
  ramp_duration: 2000

# ============================================================================
# Layer 1 Services (owned by Controller)
# ============================================================================
central_status_logger:
  id: status_logger

persistent_state_manager:
  id: psm

calendar_manager:
  id: calendar

# ============================================================================
# Layer 1: Unified Controller
# ============================================================================
plantos_controller:
  id: controller

  # Layer 3
  hal: hal

  # Layer 2
  safety_gate: safety_gate

  # Layer 1 Services
  status_logger: status_logger
  persistence: psm
  calendar: calendar

# ============================================================================
# Web UI Buttons (new API)
# ============================================================================
button:
  - platform: template
    name: "Start pH Correction"
    on_press:
      - lambda: id(controller)->startPhCorrection();

  - platform: template
    name: "Start Feeding"
    on_press:
      - lambda: id(controller)->startFeeding();

  - platform: template
    name: "Fill Tank"
    on_press:
      - lambda: id(controller)->startFillTank();

  - platform: template
    name: "Empty Tank"
    on_press:
      - lambda: id(controller)->startEmptyTank();

  - platform: template
    name: "Toggle Maintenance Mode"
    on_press:
      - lambda: id(controller)->toggleMaintenanceMode(true);

# ============================================================================
# REMOVE OLD COMPONENTS
# ============================================================================
# DELETE: controller (old hardware FSM)
# DELETE: plantos_logic (old application FSM)
# DELETE: on_boot lambda hack for logger injection
```

**Dependencies**: Phases 1-7 complete
**Testing**: YAML parse test, verify all IDs resolve

---

### Issue #8.2: Update Button IDs
**File**: `plantOS.yaml`
**Priority**: P1

**Task**: Replace all old button lambda calls with new Controller API

**Old vs New**:
```yaml
# OLD (dual FSM)
- lambda: id(my_controller)->reset_to_init();
- lambda: id(plant_logic)->start_ph_correction();

# NEW (unified Controller)
- lambda: id(controller)->resetToInit();
- lambda: id(controller)->startPhCorrection();
```

**Dependencies**: Issue #8.1
**Testing**: Verify all buttons trigger correct methods

---

## Phase 9: Remove Old Components

### Issue #9.1: Delete Old Controller Component
**Directory**: `components/controller/`
**Priority**: P0

**Task**: Remove entire old hardware FSM directory

**Files to Delete**:
- `controller.h/cpp`
- `state_init.cpp`
- `state_calibration.cpp`
- `state_ready.cpp`
- `state_error.cpp`
- `state_error_test.cpp`
- `CentralStatusLogger.h/cpp` (moved to standalone)
- `__init__.py`

**Dependencies**: Phase 8 complete, new Controller working
**Testing**: Build succeeds without old component

---

### Issue #9.2: Delete Old PlantOSLogic Component
**Directory**: `components/plantos_logic/`
**Priority**: P0

**Task**: Remove entire old application FSM directory

**Files to Delete**:
- `PlantOSLogic.h/cpp`
- `__init__.py`

**Dependencies**: Phase 8 complete, new Controller working
**Testing**: Build succeeds without old component

---

### Issue #9.3: Clean Up Unused Imports
**Files**: Various
**Priority**: P1

**Task**: Remove unused includes and namespace references

**Dependencies**: Issues #9.1, #9.2
**Testing**: No compiler warnings about unused includes

---

## Phase 10: Documentation and Optimization

### Issue #10.1: Update CLAUDE.md Architecture
**File**: `CLAUDE.md`
**Priority**: P1

**Task**: Replace dual FSM documentation with 3-layer architecture

**Sections to Update**:
1. Project Overview - mention 3-layer HAL architecture
2. System Architecture - replace dual FSM diagram with 3-layer
3. Component Reference - update Controller section, remove old PlantOSLogic
4. Key Architectural Decisions - explain HAL rationale

**Dependencies**: Phase 9 complete
**Testing**: Documentation review

---

### Issue #10.2: Create Architecture Diagrams
**Files**: `docs/architecture/`
**Priority**: P2

**Task**: Create visual diagrams for 3-layer architecture

**Diagrams**:
1. Layer interaction diagram (Controller → SafetyGate → HAL)
2. FSM state transition diagram (all 12 states)
3. Data flow diagram (sensors, actuators, services)
4. Dependency injection diagram

**Tools**: Mermaid, PlantUML, or ASCII art
**Dependencies**: Issue #10.1
**Testing**: Visual review

---

### Issue #10.3: Performance Profiling
**Priority**: P2

**Task**: Verify system maintains ~1000 Hz loop frequency

**Profiling Points**:
- Main loop iteration time
- LED behavior update time
- State handler execution time
- HAL call overhead

**Acceptance Criteria**:
- Loop runs at ≥1000 Hz (≤1ms per iteration)
- LED animations smooth (no visible stuttering)
- No blocking operations in loop()

**Dependencies**: Phase 9 complete
**Testing**: Serial log timing measurements

---

### Issue #10.4: Optimize LED Behavior Updates
**File**: `components/plantos_controller/led_behaviors/`
**Priority**: P3

**Task**: Optimize LED update calculations if needed

**Optimization Ideas**:
- Cache sin() calculations
- Reduce float operations
- Pre-compute brightness tables

**Dependencies**: Issue #10.3
**Testing**: Measure before/after performance

---

### Issue #10.5: Create Migration Guide
**File**: `docs/MIGRATION_GUIDE.md`
**Priority**: P1

**Task**: Document breaking changes and migration steps for users

**Content**:
1. Overview of changes (dual FSM → unified Controller)
2. YAML configuration migration (before/after examples)
3. Button ID changes
4. API changes (old methods → new methods)
5. Troubleshooting common issues

**Dependencies**: Phase 9 complete
**Testing**: User review

---

## Testing Strategy

### Unit Tests
- HAL interface mock for isolated testing
- LED behavior unit tests (verify patterns)
- State handler unit tests (verify timing, transitions)

### Integration Tests
- HAL → SafetyGate → Controller integration
- Sensor callback flow (pH reading → Controller)
- Service dependency injection (StatusLogger, PSM, Calendar)

### System Tests
- Full pH correction sequence (5 attempts, dosing, mixing)
- Feeding sequence (sequential nutrient pumps)
- Water management (fill, empty)
- Maintenance mode persistence (across reboot)
- Error recovery (critical pH, SafetyGate rejection)

### Performance Tests
- Loop frequency measurement (~1000 Hz target)
- LED animation smoothness
- Non-blocking timing verification

---

## Success Metrics

✅ **Architecture**:
- 3 layers clearly separated (Controller, SafetyGate, HAL)
- No direct GPIO/I2C calls above HAL layer
- Services owned by Controller, not HAL

✅ **Functionality**:
- All existing features working (pH, feeding, water, maintenance)
- LED behaviors replace old boot/error states
- Clean dependency injection (no lambda hacks)

✅ **Performance**:
- Loop runs at ≥1000 Hz
- LED animations smooth
- No blocking delays

✅ **Code Quality**:
- Modular state handlers (separate files)
- Clear interfaces (HAL, LedBehavior)
- Comprehensive logging

✅ **Documentation**:
- CLAUDE.md updated with 3-layer architecture
- Migration guide for users
- Architecture diagrams created

---

## Risk Mitigation

### High Risk Items
1. **SafetyGate HAL integration** - Could break safety guarantees
   - Mitigation: Extensive testing, preserve all safety logic
2. **Timing changes** - Could affect pH correction accuracy
   - Mitigation: Port exact timing from old code
3. **State machine bugs** - Complex transitions
   - Mitigation: Thorough state handler testing

### Rollback Plan
1. Keep old components until Phase 9
2. Parallel implementation (new Controller alongside old)
3. Git tags at each phase completion
4. YAML configuration can be reverted easily

---

## Estimated Effort

| Phase | Issues | Estimated Hours |
|-------|--------|-----------------|
| Phase 1 | 3 | 4-6 hours |
| Phase 2 | 2 | 3-4 hours |
| Phase 3 | 4 | 6-8 hours |
| Phase 4 | 5 | 8-10 hours |
| Phase 5 | 5 | 10-12 hours |
| Phase 6 | 3 | 6-8 hours |
| Phase 7 | 2 | 4-6 hours |
| Phase 8 | 2 | 2-3 hours |
| Phase 9 | 3 | 1-2 hours |
| Phase 10 | 5 | 6-8 hours |
| **TOTAL** | **34 issues** | **50-67 hours** |

---

## Next Steps

1. **Review this roadmap** with team/stakeholders
2. **Start Phase 1**: Create HAL foundation
3. **Incremental testing**: Test each phase before moving forward
4. **Daily standups**: Track progress, address blockers
5. **Code reviews**: Ensure quality at each phase

---

*Last Updated*: 2025-12-05
*Status*: Ready for Implementation
