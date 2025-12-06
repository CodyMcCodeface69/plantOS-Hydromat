# PlantOS: Unified FSM with HAL Architecture

## Goal
Merge the dual FSM architecture (Controller + PlantOSLogic) into a single unified FSM with full Hardware Abstraction Layer (HAL), isolating business logic from hardware through clean interfaces.

## User Requirements
- ✅ **Visual behaviors**: LED patterns driven by application state (not separate FSM states)
- ✅ **Full HAL abstraction**: Platform-agnostic interfaces (ILed, ISensor, IActuator, etc.)
- ✅ **Logger injection**: CentralStatusLogger properly injected via ESPHome schema
- ✅ **Breaking changes OK**: Design for cleanest architecture, no backward compatibility constraints

## Current Architecture Problems
1. **Dual FSM complexity**: Two independent state machines (Controller + PlantOSLogic)
2. **Logger injection hack**: PlantOSLogic borrows logger via lambda in YAML `on_boot`
3. **Visual states as FSM states**: INIT/CALIBRATION/ERROR shouldn't be separate states
4. **No hardware abstraction**: Direct ESPHome API coupling (`light::LightState`, etc.)
5. **Monolithic PlantOSLogic**: 976 lines in single file

---

## Proposed Architecture

### 3-Layer Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ Layer 1: UNIFIED CONTROLLER (The Brain)                    │
│ - Orchestrates system behavior                             │
│ - State transitions (INIT, IDLE, PH_ROUTINE, etc.)         │
│ - Coordinates helper services:                             │
│   • StatusLogger (reporting)                               │
│   • PersistentStateManager (crash recovery)                │
│   • Calendar (schedules, target values)                    │
└────────────────┬────────────────────────────────────────────┘
                 │
                 ▼ (Controller calls SafetyGate)
┌─────────────────────────────────────────────────────────────┐
│ Layer 2: ACTUATOR SAFETY GATE (The Guard)                  │
│ - Validates commands before hardware execution             │
│ - Enforces: Debouncing, Max Duration, Safety Locks         │
│ - Flow: Controller → SafetyGate → HAL                      │
└────────────────┬────────────────────────────────────────────┘
                 │
                 ▼ (SafetyGate calls HAL)
┌─────────────────────────────────────────────────────────────┐
│ Layer 3: HAL - Hardware Abstraction Layer (The Hands)      │
│ - Direct hardware interaction ONLY                         │
│ - Interfaces: setPump(), readPH(), setSystemLED()          │
│ - No digitalWrite/Wire.read above this layer               │
└─────────────────────────────────────────────────────────────┘
```

### Component Structure
```
components/
  plantos_controller/                # Unified Controller (Layer 1)
    __init__.py                      # ESPHome integration
    controller.h                     # Core Controller class
    controller.cpp                   # FSM driver loop

    states/                          # State handlers (modular)
      state_init.cpp                 # Boot sequence state
      state_idle.cpp                 # Idle/ready state
      state_ph_routine.cpp           # pH measuring/calculating/injecting/mixing
      state_feeding.cpp              # Feeding routine
      state_water_management.cpp     # Water fill/empty
      state_maintenance.cpp          # Maintenance mode
      state_error.cpp                # Error state

    led_behaviors/                   # LED visual system
      led_behavior.h                 # Base behavior interface
      boot_sequence.cpp              # Red→Yellow→Green (3s)
      breathing_green.cpp            # IDLE visual
      routine_indicator.cpp          # pH/feeding visuals
      error_flash.cpp                # Error indication

  actuator_safety_gate/              # Safety Gate (Layer 2)
    # KEEP EXISTING - just update to call HAL instead of GPIO

  plantos_hal/                       # HAL (Layer 3)
    hal.h                            # Main HAL interface
    hal.cpp                          # HAL implementation

    # Actuator interfaces
    actuators/
      pump_control.h/cpp             # setPump(id, state)
      valve_control.h/cpp            # setValve(id, state)

    # Sensor interfaces
    sensors/
      ph_sensor.h/cpp                # readPH(), calibrate()
      water_level.h/cpp              # readWaterLevel()

    # User feedback
    feedback/
      system_led.h/cpp               # setSystemLED(color, pattern)

    # System services
    system/
      time_manager.h/cpp             # getSystemTime()

  # Service Components (owned by Controller)
  central_status_logger/             # Reporting service
    # KEEP EXISTING

  persistent_state_manager/          # Crash recovery service
    # KEEP EXISTING

  calendar_manager/                  # Schedule service
    # KEEP EXISTING
```

---

## HAL Interface (Layer 3: Hardware Abstraction)

The HAL provides a clean C++ interface for all physical hardware. **No** `digitalWrite()`, `Wire.read()`, or `light::LightState` calls should exist above this layer.

### Main HAL Interface (`plantos_hal/hal.h`)
```cpp
namespace plantos_hal {

class HAL {
public:
    // ============================================================================
    // ACTUATORS - Called by SafetyGate ONLY
    // ============================================================================
    virtual void setPump(const std::string& pumpId, bool state) = 0;
    virtual void setValve(const std::string& valveId, bool state) = 0;
    virtual bool getPumpState(const std::string& pumpId) const = 0;
    virtual bool getValveState(const std::string& valveId) const = 0;

    // ============================================================================
    // SENSORS - Called by Controller
    // ============================================================================
    virtual float readPH() = 0;
    virtual bool hasPhValue() const = 0;
    virtual void onPhChange(std::function<void(float)> callback) = 0;
    virtual bool startPhCalibration(float calibrationPoint) = 0;

    virtual float readWaterLevel() = 0;
    virtual bool hasWaterLevel() const = 0;

    // ============================================================================
    // USER FEEDBACK - Called by Controller (LED behaviors)
    // ============================================================================
    virtual void setSystemLED(float r, float g, float b, float brightness = 1.0f) = 0;
    virtual void turnOffLED() = 0;
    virtual bool isLEDOn() const = 0;

    // ============================================================================
    // SYSTEM - Called by Controller
    // ============================================================================
    virtual uint32_t getSystemTime() const = 0;  // millis()
};

} // namespace plantos_hal
```

### ESPHome HAL Implementation (`plantos_hal/hal.cpp`)
```cpp
class ESPHomeHAL : public HAL {
public:
    ESPHomeHAL(
        esphome::light::LightState* led,
        esphome::sensor::Sensor* ph_sensor,
        // ... GPIO/PWM components for pumps/valves
    ) : led_(led), ph_sensor_(ph_sensor) {}

    // Actuators - Wrap GPIO/PWM
    void setPump(const std::string& pumpId, bool state) override {
        // Direct GPIO control or PWM
        if (pumpId == "AcidPump") {
            pump_acid_->write_state(state);
        }
        // ...
    }

    // Sensors - Wrap ESPHome sensor::Sensor
    float readPH() override {
        return ph_sensor_ ? ph_sensor_->state : 0.0f;
    }

    void onPhChange(std::function<void(float)> callback) override {
        if (ph_sensor_) {
            ph_sensor_->add_on_state_callback(callback);
        }
    }

    // User Feedback - Wrap light::LightState
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
    esphome::light::LightState* led_;
    esphome::sensor::Sensor* ph_sensor_;
    // GPIO/PWM outputs for pumps/valves
};
```

---

## Unified Controller (Layer 1: The Brain)

### State Enumeration (`plantos_controller/controller.h`)
```cpp
enum class ControllerState {
    INIT,                  // Boot sequence (visual: R→Y→G)
    IDLE,                  // System stable (breathing green LED)
    MAINTENANCE,           // Maintenance mode (solid yellow LED)
    ERROR,                 // Error state (fast red flash)

    // pH correction sequence
    PH_MEASURING,          // 5min stabilization (yellow pulse)
    PH_CALCULATING,        // Decision point (yellow fast blink)
    PH_INJECTING,          // Acid pump ON (cyan pulse)
    PH_MIXING,             // Air pump ON (blue pulse)
    PH_CALIBRATING,        // pH calibration

    // Feeding/water
    FEEDING,               // Nutrient pumps ON (orange pulse)
    WATER_FILLING,         // Fresh water valve (blue)
    WATER_EMPTYING,        // Wastewater pump (purple)
};
```

### Core Controller Class (`plantos_controller/controller.h`)
```cpp
class PlantOSController : public esphome::Component {
public:
    void setup() override;
    void loop() override;

    // ========================================================================
    // DEPENDENCY INJECTION (set from ESPHome Python)
    // ========================================================================

    // Layer 3: HAL (hardware abstraction)
    void setHAL(plantos_hal::HAL* hal);

    // Layer 2: SafetyGate (actuator guard)
    void setSafetyGate(actuator_safety_gate::ActuatorSafetyGate* gate);

    // Layer 1: Services (owned by Controller)
    void setStatusLogger(central_status_logger::CentralStatusLogger* logger);
    void setPersistenceManager(persistent_state_manager::PersistentStateManager* psm);
    void setCalendar(calendar_manager::CalendarManager* calendar);

    // ========================================================================
    // PUBLIC API (callable from web UI buttons)
    // ========================================================================
    void startPhCorrection();
    void startFeeding();
    void startFillTank();
    void startEmptyTank();
    bool toggleMaintenanceMode(bool state);
    void resetToInit();

private:
    // ========================================================================
    // DEPENDENCIES
    // ========================================================================
    plantos_hal::HAL* hal_{nullptr};                                    // Layer 3
    actuator_safety_gate::ActuatorSafetyGate* safety_gate_{nullptr};   // Layer 2
    central_status_logger::CentralStatusLogger* status_logger_{nullptr};
    persistent_state_manager::PersistentStateManager* psm_{nullptr};
    calendar_manager::CalendarManager* calendar_{nullptr};

    // ========================================================================
    // FSM STATE
    // ========================================================================
    ControllerState current_state_{ControllerState::INIT};
    uint32_t state_start_time_{0};
    uint32_t state_counter_{0};  // For once-per-second checks

    // ========================================================================
    // LED BEHAVIOR SYSTEM
    // ========================================================================
    std::unique_ptr<LedBehaviorSystem> led_behaviors_;

    // ========================================================================
    // STATE HANDLERS (in separate files)
    // ========================================================================
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

    // ========================================================================
    // ACTUATOR HELPERS (call SafetyGate → HAL)
    // ========================================================================
    bool requestPump(const std::string& pumpId, bool state, uint32_t duration = 0);
    bool requestValve(const std::string& valveId, bool state, uint32_t duration = 0);
    void turnOffAllPumps();
};
```

### Controller Loop (`plantos_controller/controller.cpp`)
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
        // ... (other states)
    }
}

// Actuator helper - Controller → SafetyGate → HAL
bool PlantOSController::requestPump(const std::string& pumpId, bool state, uint32_t duration) {
    return safety_gate_->executeCommand(pumpId, state, duration);
}
```

---

## LED Behavior System

### Concept
LED patterns are **visual behaviors triggered by Controller states**, not separate FSM states. Runs at ~1000 Hz for smooth animations via HAL.

### Base Interface (`plantos_controller/led_behaviors/led_behavior.h`)
```cpp
class LedBehavior {
public:
    virtual void onEnter() {}
    virtual void update(plantos_hal::HAL* hal, uint32_t elapsed) = 0;
    virtual void onExit() {}
    virtual bool isComplete(uint32_t elapsed) const { return false; }
};

class LedBehaviorSystem {
public:
    void update(ControllerState state, uint32_t stateElapsed, plantos_hal::HAL* hal);
    void setBehavior(std::unique_ptr<LedBehavior> behavior);
private:
    std::unique_ptr<LedBehavior> current_behavior_;
    std::map<ControllerState, std::unique_ptr<LedBehavior>> state_behaviors_;
};
```

### State → LED Behavior Mapping

| Controller State        | LED Behavior          | Color  | Pattern              | Duration    |
|------------------------|-----------------------|--------|----------------------|-------------|
| INIT                   | BootSequence          | RGB    | R→Y→G                | 3s          |
| IDLE                   | BreathingGreen        | Green  | Sine wave breathing  | Continuous  |
| MAINTENANCE            | SolidYellow           | Yellow | Solid                | Continuous  |
| ERROR                  | ErrorFlash            | Red    | Fast flash (5 Hz)    | 5s          |
| PH_MEASURING           | YellowPulse           | Yellow | Slow pulse (0.5 Hz)  | 5min        |
| PH_CALCULATING         | YellowFastBlink       | Yellow | Fast blink (2 Hz)    | <1s         |
| PH_INJECTING           | CyanPulse             | Cyan   | Slow pulse           | Variable    |
| PH_MIXING              | BluePulse             | Blue   | Slow pulse           | 2min        |
| FEEDING                | OrangePulse           | Orange | Slow pulse           | Variable    |
| WATER_FILLING          | BlueSolid             | Blue   | Solid                | 30s         |
| WATER_EMPTYING         | PurplePulse           | Purple | Slow pulse           | 30s         |

### Example Behaviors

**Breathing Green** (`led_behaviors/breathing_green.cpp`):
```cpp
class BreathingGreenBehavior : public LedBehavior {
    void update(plantos_hal::HAL* hal, uint32_t elapsed) override {
        float t = elapsed / 1000.0f;
        float brightness = (std::sin(t * 3.14159f) + 1.0f) / 2.0f;
        brightness = 0.1f + (brightness * 0.9f);  // 10% to 100%
        hal->setSystemLED(0.0f, 1.0f, 0.0f, brightness);
    }
};
```

**Boot Sequence** (`led_behaviors/boot_sequence.cpp`):
```cpp
class BootSequenceBehavior : public LedBehavior {
    void update(plantos_hal::HAL* hal, uint32_t elapsed) override {
        if (elapsed < 1000) {
            hal->setSystemLED(1.0f, 0.0f, 0.0f);  // Red
        } else if (elapsed < 2000) {
            hal->setSystemLED(1.0f, 1.0f, 0.0f);  // Yellow
        } else {
            hal->setSystemLED(0.0f, 1.0f, 0.0f);  // Green
        }
    }
    bool isComplete(uint32_t elapsed) const override { return elapsed >= 3000; }
};
```

---

## ESPHome Integration

### YAML Configuration (New Schema)
```yaml
# Layer 3: HAL
plantos_hal:
  id: hal
  # Hardware components
  system_led: system_led          # WS2812 RGB
  ph_sensor: ezo_ph_sensor        # EZO pH circuit
  # GPIO/PWM outputs for pumps/valves configured here
  acid_pump_gpio: gpio_acid_pump
  nutrient_a_gpio: gpio_nutrient_a
  # ... (other GPIOs)

# Layer 2: Safety Gate (keep existing)
actuator_safety_gate:
  id: safety_gate
  hal: hal  # Now calls HAL instead of direct GPIO
  acid_pump_max_duration: 30
  nutrient_pump_max_duration: 60
  # ... (existing safety config)

# Layer 1 Services (keep existing)
central_status_logger:
  id: status_logger

persistent_state_manager:
  id: psm

calendar_manager:
  id: calendar

# Layer 1: Unified Controller
plantos_controller:
  id: controller

  # Layer 3: HAL
  hal: hal

  # Layer 2: SafetyGate
  safety_gate: safety_gate

  # Layer 1: Services
  status_logger: status_logger
  persistence: psm
  calendar: calendar

# Web UI Buttons (new API)
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
    name: "Toggle Maintenance Mode"
    on_press:
      - lambda: id(controller)->toggleMaintenanceMode(true);
```

### Python Integration (`plantos_controller/__init__.py`)
```python
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Layer 3: HAL injection
    hal = await cg.get_variable(config['hal'])
    cg.add(var.setHAL(hal))

    # Layer 2: SafetyGate injection
    gate = await cg.get_variable(config['safety_gate'])
    cg.add(var.setSafetyGate(gate))

    # Layer 1: Service injections (no lambda hack!)
    logger = await cg.get_variable(config['status_logger'])
    cg.add(var.setStatusLogger(logger))

    psm = await cg.get_variable(config['persistence'])
    cg.add(var.setPersistenceManager(psm))

    calendar = await cg.get_variable(config['calendar'])
    cg.add(var.setCalendar(calendar))
```

### SafetyGate Update (`actuator_safety_gate/__init__.py`)
```python
# SafetyGate now receives HAL to call instead of GPIO
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Inject HAL
    hal = await cg.get_variable(config['hal'])
    cg.add(var.setHAL(hal))

    # ... existing config
```

---

## Migration Strategy

### Phase 1: Create HAL (Layer 3)
1. Create `plantos_hal/` component
2. Define `HAL` interface in `hal.h`
3. Implement `ESPHomeHAL` in `hal.cpp`
4. Wrap: LED (light::LightState), pH sensor (sensor::Sensor), system time (millis())
5. Add pump/valve stub methods (will connect to SafetyGate later)
6. **Testing**: Compile-only test, verify interface compiles

### Phase 2: Update SafetyGate to Use HAL (Layer 2)
1. Modify `actuator_safety_gate/ActuatorSafetyGate.h` to receive `HAL*`
2. Replace direct GPIO/PWM calls with `hal->setPump()`, `hal->setValve()`
3. Update `__init__.py` to inject HAL dependency
4. **Testing**: Verify SafetyGate → HAL call chain works

### Phase 3: Create LED Behavior System
1. Implement `led_behavior.h` base classes in `plantos_controller/led_behaviors/`
2. Implement concrete behaviors (BootSequence, BreathingGreen, YellowPulse, etc.)
3. Implement `LedBehaviorSystem` manager with state→behavior mapping
4. **Testing**: Standalone test with HAL mock

### Phase 4: Create Unified Controller (Layer 1) - Parallel Implementation
1. Create `plantos_controller/` component alongside old components
2. Implement `PlantOSController` class with 3-layer dependency injection
3. Port INIT state with boot sequence LED behavior
4. Port IDLE state with breathing green LED behavior
5. Wire up service dependencies (StatusLogger, PSM, Calendar)
6. **Testing**: INIT → IDLE transition works, LED shows correct patterns

### Phase 5: Port pH Correction States
1. Port PH_MEASURING, PH_CALCULATING, PH_INJECTING, PH_MIXING states
2. Use `requestPump()` helper (Controller → SafetyGate → HAL)
3. Read pH via `hal->readPH()`
4. Maintain same timing (5min stabilization, 2min mixing)
5. Use PSM for crash recovery logging
6. Use Calendar for target pH ranges
7. **Testing**: pH correction sequence end-to-end

### Phase 6: Port Feeding and Water Management
1. Port FEEDING, WATER_FILLING, WATER_EMPTYING states
2. Use `requestPump()` and `requestValve()` helpers
3. Use Calendar for nutrient dosing durations
4. **Testing**: Manual triggers via buttons

### Phase 7: Port Maintenance and Error States
1. Port MAINTENANCE state with persistent shutdown (via PSM)
2. Port ERROR state with red flash LED behavior
3. Implement error conditions (pH critical, sensor failure)
4. **Testing**: Toggle maintenance mode, trigger error conditions

### Phase 8: Migrate YAML Configuration
1. Update `plantOS.yaml` with 3-layer schema:
   - Layer 3: `plantos_hal`
   - Layer 2: `actuator_safety_gate` (with HAL injection)
   - Layer 1: `plantos_controller` (with all dependencies)
2. Remove old `controller` and `plantos_logic` configurations
3. Update button IDs and lambda calls
4. **Testing**: Verify all buttons work with new API

### Phase 9: Remove Old Components
1. Delete `components/controller/`
2. Delete `components/plantos_logic/`
3. Clean up unused imports
4. **Testing**: Full system regression test

### Phase 10: Documentation and Optimization
1. Profile performance (ensure ~1000 Hz loop maintained)
2. Optimize LED behavior updates if needed
3. Update CLAUDE.md with 3-layer architecture
4. Add architecture diagrams
5. Update web UI labels for clarity

---

## Critical Files

### To Create (New Components)
1. **`components/plantos_hal/hal.h`** - HAL interface definition (Layer 3)
2. **`components/plantos_hal/hal.cpp`** - ESPHome HAL implementation
3. **`components/plantos_hal/__init__.py`** - ESPHome Python integration for HAL
4. **`components/plantos_controller/controller.h`** - Unified Controller class (Layer 1)
5. **`components/plantos_controller/controller.cpp`** - Controller FSM driver
6. **`components/plantos_controller/__init__.py`** - ESPHome Python integration
7. **`components/plantos_controller/led_behaviors/led_behavior.h`** - LED behavior system base
8. **`components/plantos_controller/led_behaviors/boot_sequence.cpp`** - Boot LED behavior
9. **`components/plantos_controller/led_behaviors/breathing_green.cpp`** - IDLE LED behavior
10. **`components/plantos_controller/states/state_init.cpp`** - INIT state handler
11. **`components/plantos_controller/states/state_idle.cpp`** - IDLE state handler
12. **`components/plantos_controller/states/state_ph_routine.cpp`** - pH states handler

### To Modify (Existing Components)
1. **`components/actuator_safety_gate/ActuatorSafetyGate.h`** - Add HAL* dependency (Layer 2)
2. **`components/actuator_safety_gate/ActuatorSafetyGate.cpp`** - Replace GPIO with hal->setPump()
3. **`components/actuator_safety_gate/__init__.py`** - Inject HAL dependency
4. **`plantOS.yaml`** - New 3-layer configuration schema (breaking changes)
5. **`CLAUDE.md`** - Update architecture documentation with 3-layer design

### To Delete (Phase 9)
1. **`components/controller/`** - Old hardware FSM (entire directory)
2. **`components/plantos_logic/`** - Old application FSM (entire directory)

### To Preserve (Service Components)
1. **`components/central_status_logger/`** - Keep as-is, used by Controller
2. **`components/persistent_state_manager/`** - Keep as-is, used by Controller
3. **`components/calendar_manager/`** - Keep as-is, used by Controller
4. **`components/ezo_ph/`** - Keep as-is, wrapped by HAL
5. **`components/sensor_filter/`** - Keep as-is, wrapped by HAL

---

## Key Design Decisions

### 1. 3-Layer Architecture (Controller → SafetyGate → HAL)
**Rationale**: Clean separation of concerns - business logic (Controller), safety validation (SafetyGate), hardware abstraction (HAL)
**Flow**: Controller requests actuator operation → SafetyGate validates → HAL executes
**Trade-off**: Extra indirection, but worth it for maintainability and safety guarantees

### 2. Services Separate from HAL
**Rationale**: StatusLogger, PSM, Calendar are business logic services, not hardware abstractions
**Design**: Controller owns/coordinates services, HAL only handles hardware (sensors, actuators, LED, time)
**Trade-off**: More dependencies to inject, but clearer architectural boundaries

### 3. Switch-Case FSM (not function pointers)
**Rationale**: Simpler for application logic (12 states), better debugging, matches ESPHome patterns
**Trade-off**: Less "elegant" than function pointers, but more maintainable and easier to set breakpoints

### 4. Keep ActuatorSafetyGate as Layer 2
**Rationale**: Proven safety component, high-risk to refactor, becomes middleware between Controller and HAL
**Update**: SafetyGate now calls HAL instead of direct GPIO, preserving all safety features
**Trade-off**: Slight adapter overhead, but preserves critical safety guarantees

### 5. LED Behaviors as Background System
**Rationale**: Visual feedback orthogonal to FSM state, not separate FSM states (user requirement)
**Implementation**: LED behaviors update every loop based on current Controller state
**Trade-off**: Slight timing complexity, but cleaner separation and meets user requirements

### 6. HAL Uses Raw Pointers
**Rationale**: Matches ESPHome ecosystem patterns, avoids smart pointer overhead on embedded systems
**Trade-off**: No compile-time ownership guarantees, but standard practice in ESPHome

### 7. Single HAL Interface (not multiple interfaces)
**Rationale**: Simpler to pass one HAL* instead of ILed*, ISensor*, IActuator* separately
**Design**: HAL provides all hardware methods (setPump, readPH, setSystemLED, etc.)
**Trade-off**: Larger interface, but easier dependency injection

---

## Success Criteria

✅ **3-Layer Architecture Implemented**:
  - Layer 1: Unified Controller (orchestrates system behavior)
  - Layer 2: ActuatorSafetyGate (validates commands)
  - Layer 3: HAL (hardware abstraction - no direct GPIO/I2C above this layer)

✅ **Single Unified Controller** replaces dual FSM architecture (Controller + PlantOSLogic merged)

✅ **Services Separate from HAL**:
  - StatusLogger, PSM, Calendar owned/coordinated by Controller
  - HAL only handles hardware (sensors, actuators, LED, time)

✅ **LED Behaviors as Background System**:
  - Visual patterns driven by Controller state (not separate FSM states)
  - Boot sequence, breathing, pulses implemented as behaviors

✅ **Clean Dependency Injection**:
  - Controller receives: HAL*, SafetyGate*, StatusLogger*, PSM*, Calendar*
  - SafetyGate receives: HAL*
  - No lambda hacks, all via ESPHome schema

✅ **Actuator Flow**: Controller → SafetyGate → HAL
  - All safety features preserved (debouncing, duration limits, soft-start/stop)

✅ **All Functionality Preserved**:
  - pH correction sequence (5min stabilization, dosing, mixing, max 5 attempts)
  - Nutrient feeding (schedule-driven)
  - Water management (fill/empty)
  - Maintenance mode with NVS persistence
  - Crash recovery via PSM

✅ **Performance Maintained**:
  - System loop runs at ~1000 Hz
  - LED animations smooth
  - Non-blocking timing throughout

✅ **Web UI Working**:
  - All buttons functional with new API
  - Manual triggers: pH correction, feeding, water management, maintenance mode

✅ **Documentation Updated**:
  - CLAUDE.md reflects 3-layer architecture
  - YAML migration guide provided
  - Architecture diagrams added
