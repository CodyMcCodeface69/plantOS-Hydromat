# Changelog

All notable changes to the PlantOS project will be documented in this file.

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
