# Changelog

All notable changes to the PlantOS project will be documented in this file.

## [v0.3.0] - 2025-11-29

### Description
Major feature release adding centralized actuator safety control system for critical hardware protection, implemented as a native ESPHome component with automated testing.

### Added
- **ActuatorSafetyGate Component**: Full ESPHome component implementation for controlling all system actuators with comprehensive safety features:
  - **Debouncing Protection**: Prevents redundant commands by tracking last requested state per actuator
  - **Maximum Duration Enforcement**: Enforces configurable runtime limits for critical actuators (pumps, valves) to prevent overruns and hardware damage
  - **Runtime Tracking**: Real-time monitoring of actuator runtime with violation detection
  - **Safety Violation Logging**: Comprehensive logging of all command rejections with clear reason messages
  - **Emergency Override**: Force reset capability for emergency shutoff scenarios
  - **Statistics API**: Query actuator state, runtime, and configuration
  - **ESPHome Integration**: Native component with Python integration (`__init__.py`) for YAML configuration

- **DummyActuatorTrigger Component**: Automated test component for validating safety gate functionality:
  - **Test Sequence 1 - Debouncing**: Validates duplicate command rejection
  - **Test Sequence 2 - Duration Limits**: Validates max duration enforcement (5s limit tested with 3s and 10s requests)
  - **Test Sequence 3 - Normal Operation**: Validates standard ON/OFF cycles
  - **Visual Feedback**: Controls LED on GPIO 4 to show safety gate approvals/rejections
  - **Automated Testing**: Runs test sequences every 15 seconds (configurable)

- **Hardware Configuration**: Test LED on GPIO 4 with wiring diagram and setup instructions

- **Documentation**:
  - ActuatorSafetyGate: README.md with complete API reference and usage examples
  - ActuatorSafetyGate: INTEGRATION_GUIDE.md with three integration methods (Lambda-based, Custom Component, Direct C++)
  - ActuatorSafetyGate: example_usage.cpp with 10+ practical examples including pH control and watering systems
  - DummyActuatorTrigger: README.md with test sequence documentation and troubleshooting guide

### Technical Details
- Core method: `bool executeCommand(const char* actuatorID, bool targetState, int maxDurationSeconds = 0)`
- Proper ESPHome namespacing: `esphome::actuator_safety_gate`
- Inherits from `Component` with `setup()` and `loop()` lifecycle methods
- Supports per-actuator configuration with `setMaxDuration()`
- Non-blocking periodic monitoring via `loop()` method
- Full ESPHome YAML configuration support
- Configured in `plantOS.yaml` lines 311-339

---

## [v0.2.1] - 2025-11-29

### Description
Cleanup release removing redundant logging components after central status logger integration.

### Changed
- **IP Logger Disabled**: Disabled the `ip_logger` component as its functionality is now fully integrated into the central status logger, which provides comprehensive system status including IP address, web server status, sensor readings, FSM state, and alerts in a unified format every 30 seconds.

### Fixed
- **Verified Central Status Logger**: Confirmed that the central status logger is correctly implemented using ESPHome's logging API and properly integrated with the controller FSM.

---

## [v0.2] - 2025-11-29

### Description
Feature update with time synchronization, enhanced sensor filtering, and improved monitoring capabilities.

### Added
- **NTP Time Synchronization**: Implemented network time protocol synchronization for accurate timestamping using `time(NULL)`.
- **Robust Average Filter**: Implemented robust moving average filtering for sensor data to improve stability and reduce noise.
- **Extended Task Runner**: Added new Task Runner commands for debugging and maintenance:
  - `snoop`: Stream device logs for debugging
  - `status`: Check device and connection status
  - `reboot`: Reboot the device
  - `config_validate`: Task to check the plantOS.yaml configuration file for syntax and structural errors before attempting a full build or flash
- **Central Status Logger**: Implemented centralized status logging system with IP address logging and web server status functionality for better system monitoring.

### Known Bugs
- web server stopped working

---

## [v0.1] - 2025-11-22

### Description
Initial release of PlantOS firmware project. This version represents the foundational project structure.

**Initial fork of the plantOS GitHub project by tobjaw**

### Features
- ESP32-C6 based plant monitoring system
- Custom ESPHome component architecture
- Finite state machine controller with LED visual feedback
- Dummy sensor component for testing
- WiFi connectivity and OTA updates
- Web server interface on port 80

### Known Bugs
- No persistent storage of configuration data (NVS/EEPROM).
