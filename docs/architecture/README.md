# PlantOS Architecture Documentation

This directory contains detailed architecture diagrams and documentation for the PlantOS hydroponic control system.

## Architecture Overview

PlantOS uses a **3-layer HAL (Hardware Abstraction Layer) architecture** for clean separation of concerns:

```
Layer 1: Unified Controller (The Brain)
         ↓
Layer 2: Actuator Safety Gate (The Guard)
         ↓
Layer 3: HAL - Hardware Abstraction (The Hands)
```

## Documentation Files

### 01-layer-interaction.md
**Purpose**: Shows how the three architectural layers interact with each other

**Contents**:
- Layer interaction diagram (Controller → SafetyGate → HAL)
- Key interaction patterns
- Benefits of layered architecture

**Read this first** to understand the overall system structure.

### 02-fsm-state-transitions.md
**Purpose**: Documents all 12 states of the Unified Controller FSM

**Contents**:
- Complete FSM state transition diagram
- Detailed state descriptions
- LED patterns for each state
- State handler implementation patterns
- Critical paths (pH correction loop, error recovery, maintenance mode)

**Read this** to understand controller behavior and state transitions.

### 03-data-flow.md
**Purpose**: Shows how data flows from sensors through processing to actuators

**Contents**:
- Complete data flow diagram
- Sensor reading path (I2C → Filter → HAL → Controller)
- Actuator control path (Controller → SafetyGate → HAL → Hardware)
- Service integration paths (Calendar, PSM, StatusLogger)
- Timing characteristics and update frequencies

**Read this** to understand data paths and timing requirements.

### 04-dependency-injection.md
**Purpose**: Documents how components are wired together via dependency injection

**Contents**:
- Dependency injection flow (YAML → Python → C++)
- Required vs optional vs owned dependencies
- Dependency graph
- Benefits of dependency injection
- Error handling patterns

**Read this** to understand how components connect and how to add new dependencies.

## Quick Reference

### Adding a New Component

1. **Create C++ header/implementation**
   - Define class inheriting from `Component`
   - Add setter methods for dependencies
   - Implement `setup()` and `loop()`

2. **Create Python integration** (`__init__.py`)
   - Define CONFIG_SCHEMA with dependencies
   - Implement `to_code()` for dependency injection

3. **Update YAML configuration**
   - Add component configuration
   - Specify dependencies by ID

4. **Update architecture diagrams** (this directory)
   - Add to layer interaction diagram
   - Update data flow if needed
   - Update dependency injection diagram

### Understanding a State

To understand what happens in a specific controller state:

1. **Check state description** in `02-fsm-state-transitions.md`
2. **Find state transitions** in the FSM diagram
3. **Trace data flow** in `03-data-flow.md`
4. **Read implementation** in `components/plantos_controller/controller.cpp`

Example: Understanding `PH_MEASURING` state:
- **Description**: 5-minute stabilization, all pumps OFF, collect readings every 60s
- **LED Pattern**: Yellow pulse (0.5 Hz)
- **Transitions**: → PH_CALCULATING (after 5 min) or → ERROR (critical pH)
- **Data Flow**: pH Sensor → HAL → Controller → collect in ph_readings_ vector
- **Implementation**: `PlantOSController::handlePhMeasuring()` in controller.cpp

### Tracing a Code Path

To trace how a user action flows through the system:

1. **Start at YAML** (plantOS.yaml) - button configuration
2. **Find lambda call** - e.g., `id(unified_controller)->startPhCorrection()`
3. **Find public API** - `components/plantos_controller/controller.h` public methods
4. **Follow implementation** - `controller.cpp` method implementation
5. **Trace state transitions** - Use FSM diagram in `02-fsm-state-transitions.md`
6. **Check data flow** - Use `03-data-flow.md` for sensor/actuator paths

Example: User presses "Start pH Correction" button:
1. **YAML**: Button calls `id(unified_controller)->startPhCorrection()`
2. **API**: `PlantOSController::startPhCorrection()` public method
3. **Implementation**: Resets counters, transitions to `PH_MEASURING`
4. **State**: `handlePhMeasuring()` → `handlePhCalculating()` → `handlePhInjecting()` → `handlePhMixing()` → loop
5. **Data Flow**: Controller → SafetyGate → HAL → Acid Pump ON

## Diagram Conventions

### Mermaid Diagrams

All diagrams use Mermaid syntax for GitHub/markdown rendering:

- **Rectangles**: Components or classes
- **Rounded rectangles**: Processes or states
- **Arrows**: Data flow or method calls
- **Dashed arrows**: Dependency injection
- **Colored backgrounds**: Architectural layers
  - Green: Layer 1 (Controller)
  - Yellow/Orange: Layer 2 (SafetyGate)
  - Blue: Layer 3 (HAL)
  - Purple: Services
  - Red: Hardware

### State Diagrams

- **States**: Rounded rectangles with state name
- **Transitions**: Arrows with trigger condition
- **Notes**: Right-aligned boxes with state details
- **Initial state**: Solid circle
- **Final state**: Double circle (not used in our FSM - runs continuously)

## Architecture Principles

### 1. Separation of Concerns
Each layer has a single, well-defined responsibility:
- **Controller**: Business logic and state management
- **SafetyGate**: Hardware safety validation
- **HAL**: Hardware abstraction

### 2. Dependency Injection
- Components receive dependencies via setters (not construction)
- Dependencies explicit in YAML configuration
- Enables testing via mocking

### 3. Hardware Independence
- No direct GPIO/I2C access above HAL layer
- Controller works with any HAL implementation
- Easy to port to different hardware

### 4. Safety by Design
- All hardware access flows through validation layers
- Multiple defense-in-depth safety mechanisms
- Fail-safe error handling

### 5. Non-Blocking Design
- No blocking delays in any component
- All timing uses millis() comparison
- System stays responsive (WiFi, OTA, logging)

## Related Documentation

- **CLAUDE.md**: High-level project overview and component reference
- **FSMUNIFICATIONPLAN.md**: Original architecture design and rationale
- **TODOFSMUNIFICATION.md**: Implementation tracking and completion status
- **components/*/README.md**: Component-specific documentation (if exists)

## Maintenance

When updating the architecture:

1. **Update code first**: Implement changes in C++/Python
2. **Update diagrams**: Modify affected diagrams in this directory
3. **Update CLAUDE.md**: Sync high-level documentation
4. **Git commit**: Include diagram updates with code changes

This ensures documentation stays in sync with implementation.

## Tools

### Mermaid Live Editor
https://mermaid.live/

Use this to:
- Edit Mermaid diagrams visually
- Preview changes before committing
- Export diagrams as PNG/SVG (if needed)

### GitHub Rendering
GitHub automatically renders Mermaid diagrams in markdown files. Just view the .md files on GitHub to see the diagrams.

## Future Work

Potential diagram additions:
- Component lifecycle diagram (setup → loop order)
- Memory layout diagram (RAM/Flash usage)
- I2C bus timing diagram (for EZO pH sensor)
- Safety validation flowchart (SafetyGate decision tree)
- Calibration sequence diagram (pH sensor calibration)
