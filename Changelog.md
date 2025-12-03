# Changelog

All notable changes to PlantOS will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.4.0] - 2025-12-02

### Added
- Controller verbose mode for detailed logging and debugging
  - New `verbose` configuration option in plantOS.yaml (default: false)
  - Logs all state transitions with time spent in each state
  - Logs every action taken (LED updates, sensor checks, etc.)
  - Logs timing information for each action (execution duration)
  - Helps with debugging FSM behavior and performance profiling
  - All verbose logs use DEBUG level with [VERBOSE] prefix
- Runtime control of verbose mode via web UI
  - New "Toggle Verbose Logging" button in web UI
  - New "Verbose Mode Status" text sensor showing ON/OFF state
  - `toggle_verbose()` and `get_verbose()` public API methods
  - Can enable/disable verbose logging without reflashing firmware
  - Immediate effect on all subsequent logging

### Changed
- Enhanced controller logging infrastructure with timing helpers
  - Added `log_action_start()` and `log_action_end()` helper methods
  - State transition logging now includes duration when verbose mode enabled
  - All state implementations (INIT, CALIBRATION, READY, ERROR, ERROR_TEST) support verbose logging
  - Smart log level selection based on duration:
    - Actions >= 30ms: WARN level (exceeds ESPHome recommendation)
    - Actions >= 10ms: INFO level (noticeable delay)
    - Actions < 10ms: DEBUG level (normal, not logged with INFO level)
  - Added comprehensive timing for:
    - Individual state execution
    - Status logger calls (every 30s)
    - Overall loop() execution time
  - Helps identify performance bottlenecks causing "Component took too long" warnings

## [0.3.3] - 2025-12-02

### Changed
- Modified `task run` to default to serial/USB flashing without user prompt
- Added OTA flashing option via `task run OTA=true` command
- Serial device auto-detection now matches behavior from `snoop` task
- Eliminated interactive flash method selection prompt

## [0.3.2] - Previous Release

### Added
- WDT Manager with user-based subscription
- PersistentStateManager Component and PSMChecker Test Component
- ActuatorSafetyGate with DummyActuatorTrigger

### Fixed
- Web server working in mesh network again
- Disabled WDT spam for cleaner logs
