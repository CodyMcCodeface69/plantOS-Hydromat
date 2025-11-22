# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

PlantOS is an ESPHome-based firmware project for ESP32-C6 MCUs, implementing a plant monitoring and control system. The project uses a custom component architecture with a finite state machine controller that manages LED indicators based on sensor data.

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

## Project Architecture

### Configuration Files

- `plantOS.yaml`: Main ESPHome configuration defining hardware, components, and their interconnections
- `secrets.yaml`: WiFi credentials and other secrets (gitignored, see `secrets.example.yaml`)
- `flake.nix`: Nix development environment and build configuration
- `Taskfile.yml`: Task automation definitions

### Custom Components

Components are located in `components/` and follow ESPHome's external component structure. Each component has:
- Python code (`__init__.py`, `*.py`): ESPHome code generation and YAML config schema
- C++ code (`*.h`, `*.cpp`): Runtime implementation for ESP32

#### sensor_dummy

A simple polling sensor component that cycles values 0-100 in increments of 10.

**Files:**
- `sensor.py`: Configuration schema
- `sensor_dummy.h/cpp`: Implementation inheriting from `sensor::Sensor` and `PollingComponent`

**Usage in YAML:**
```yaml
sensor:
  - id: sensor_dummy_id
    platform: sensor_dummy
    name: "My Dummy Value"
    update_interval: 1s
```

#### controller

The main logic controller implementing a finite state machine (FSM) with LED visual feedback.

**Files:**
- `__init__.py`: Config schema requiring a `sensor_source` and `light_target`
- `controller.h/cpp`: FSM implementation

**Architecture:**
- FSM pattern using function pointers (`StateHandler`)
- State transitions in `loop()` based on returned next state
- Sensor data received via callback registered in `setup()`
- Light control via ESPHome's `light::LightState` API

**States:**
1. `state_init`: Boot sequence showing red → yellow → green
2. `state_calibration`: Blinking yellow for 4 seconds
3. `state_ready`: Breathing green effect with 5% random error probability per second
4. `state_error`: Fast red flashing for 5 seconds, then returns to init

**Usage in YAML:**
```yaml
controller:
  id: my_logic_controller
  sensor_source: sensor_dummy_id  # References a sensor component ID
  light_target: system_led         # References a light component ID
```

### Component Development Patterns

When creating new ESPHome components:

1. **Python side** (`__init__.py`):
   - Define namespace and C++ class bindings using `cg.esphome_ns.namespace()`
   - Extend appropriate base schemas (SENSOR_SCHEMA, COMPONENT_SCHEMA, etc.)
   - Use `cv.use_id()` to reference other components by ID
   - Register component and link dependencies in `to_code()`

2. **C++ side**:
   - Inherit from appropriate ESPHome base classes (`Component`, `Sensor`, `PollingComponent`, etc.)
   - Override lifecycle methods: `setup()`, `loop()`, `update()`
   - Use `ESP_LOGD/I/W/E()` for logging with appropriate tags
   - Access linked components via pointer members set from Python codegen

3. **Component linking**:
   - Components reference each other by ID in YAML
   - Python code uses `cv.use_id()` for validation and `cg.get_variable()` to retrieve references
   - C++ receives pointers via setter methods (e.g., `set_sensor_source()`)

## Hardware Configuration

- **Board**: ESP32-C6-DevKitC-1 (variant: esp32c6)
- **Framework**: ESP-IDF (recommended version)
- **Built-in LED**: WS2812 RGB on pin 8
- **WiFi**: Configured via secrets.yaml
- **OTA**: ESPHome platform enabled
- **Web Server**: Port 80

## Testing

No automated tests are currently in the project. Testing is done by:
1. Building and flashing to hardware
2. Observing LED behavior and log output
3. Using the web server interface on port 80
