# Changelog

All notable changes to the PlantOS project will be documented in this file.

## [v0.3.2] - 2025-11-30

### Description
Feature release adding Hardware Watchdog Timer (WDT) management for system crash detection and automatic recovery. Test components deactivated to reduce system load.

### Added
- **WDTManager Component**: Full hardware watchdog timer implementation for crash detection and automatic recovery:
  - **Hardware WDT Initialization**: Configures ESP-IDF Task Watchdog Timer (TWDT) with 10-second timeout
  - **User-Based Subscription**: Uses `esp_task_wdt_add_user()` instead of task-based subscription to prevent idle task interference
  - **Panic Mode**: `trigger_panic = true` ensures WDT timeout triggers hardware reset (not just warning)
  - **Regular Feeding**: Non-blocking watchdog feed every 500ms using millis() timing
  - **Test Mode**: Simulates system crash after 20 seconds by stopping WDT feeds
  - **Automatic Reset**: Device automatically reboots ~10 seconds after feeding stops
  - **ESP-IDF Integration**: Properly handles auto-initialized TWDT via reconfiguration
  - **Configuration Options**:
    - `timeout`: WDT timeout period (default: 10s)
    - `feed_interval`: How often to feed WDT (default: 500ms)
    - `test_mode`: Enable crash simulation (default: false)
    - `crash_delay`: Time before simulated crash (default: 20s)

### Changed
- **PSM Checker Deactivated**: Disabled PSMChecker test component (plantOS.yaml:409-414)
- **Actuator Safety Gate Deactivated**: Disabled ActuatorSafetyGate test component (plantOS.yaml:317-319)
- **Dummy Actuator Trigger Deactivated**: Disabled DummyActuatorTrigger test component (plantOS.yaml:342-347)

### Technical Details
- **Key Challenge**: Task-based WDT subscription allowed idle tasks to feed WDT, preventing timeout
- **Solution**: User-based subscription (`esp_task_wdt_add_user()`) provides exclusive control
- **User Handle**: `esp_task_wdt_user_handle_t wdt_user_handle_` stores user subscription
- **Feed Method**: `esp_task_wdt_reset_user(handle)` feeds only our user, not the entire task
- **TWDT Reconfiguration**: Uses `esp_task_wdt_reconfigure()` to update existing TWDT configuration
- **Panic Enforcement**: Configuration includes `trigger_panic = true` and `idle_core_mask = 0`
- **Log Messages**:
  - Setup: "WDT reconfigured (timeout: 10000 ms, panic mode: enabled)"
  - Operation: "WDT fed (time to crash: X s)" every 500ms
  - Crash: "===== SIMULATED CRASH ===== / Stopping WDT feeding to test automatic reset"
- **Component Files**:
  - `components/wdt_manager/__init__.py`: ESPHome configuration schema
  - `components/wdt_manager/wdt_manager.h`: C++ class declaration with user handle
  - `components/wdt_manager/wdt_manager.cpp`: Implementation with user-based subscription
- **Configuration**: Added to `plantOS.yaml` lines 444-449 with test mode enabled

### Testing Instructions
1. **Flash Firmware**: Upload firmware with `test_mode: true`
2. **Monitor Logs**: Watch serial output for WDT initialization and feeding
3. **Observe Feeding**: "WDT fed (time to crash: X s)" messages every 500ms
4. **Wait for Crash**: After 20 seconds, see "SIMULATED CRASH" message
5. **Verify Reset**: Device should automatically reboot ~10 seconds after crash message
6. **Production Use**: Set `test_mode: false` to disable crash simulation

### Use Cases
- Detect and recover from infinite loops
- Monitor system responsiveness
- Prevent hung tasks from freezing the system
- Automatic recovery from software crashes
- Critical safety shutdown for unresponsive systems

### Production Deployment
For production use, disable test mode in `plantOS.yaml`:
```yaml
wdt_manager:
  id: my_wdt
  timeout: 10s
  feed_interval: 500ms
  test_mode: false  # Disable crash simulation
```

---

## [v0.3.1] - 2025-11-29

### Description
Major feature release adding persistent state management with NVS (Non-Volatile Storage) for critical event recovery after power loss or unexpected reboots.

### Added
- **PersistentStateManager Component**: Full NVS-based persistence system for critical event logging:
  - **Event Structure**: `CriticalEventLog` with event ID (32 chars), Unix timestamp, and status code
  - **NVS Persistence**: Uses ESP32 Non-Volatile Storage via ESPHome Preferences API
  - **Recovery Detection**: `wasInterrupted(maxAgeSeconds)` method detects interrupted operations on boot
  - **Time-Based Validation**: Event age checking with NTP synchronization
  - **API Methods**:
    - `logEvent(id, status)`: Log critical event before operation
    - `clearEvent()`: Clear event after successful completion
    - `wasInterrupted(seconds)`: Check for interrupted operation
    - `getLastEvent()`: Retrieve event details for recovery
    - `getEventAge()`: Get event age in seconds
  - **Atomic Operations**: All NVS writes are atomic (complete or not written)
  - **Status Codes**: 0=STARTED, 1=COMPLETED, 2=ERROR, 3=PAUSED, 4=CANCELLED

- **PSMChecker Test Component**: Automated validation of persistent state recovery:
  - **10-Second Boot Delay**: All PSM messages appear 10 seconds after boot for clean startup logs
  - **Status Report Integration**: Messages display in PLANTOS SYSTEM STATUS REPORT as alerts
  - **Boot 1 Sequence** (10 seconds after boot):
    - Logs "PSM_TEST" event to NVS with current timestamp
    - Triggers controller ERROR_TEST state (blue/purple pulsing LED)
    - Adds alert: "⚡ PLEASE UNPLUG DEVICE NOW!" to status report
  - **Boot 2 Sequence (after replug)** (10 seconds after boot):
    - Detects "PSM_TEST" event recovered from NVS
    - Verifies event timestamp and age
    - Adds alert: "✓ PSM TEST SUCCESSFUL! Event recovered from NVS after reboot"
    - Clears test event from NVS
  - **Integration**: Links to PSM and Controller components, uses CentralStatusLogger for alerts

- **ERROR_TEST State**: New FSM state for PSM testing:
  - **Visual**: Pulsing blue/purple LED (2-second breathing cycle)
  - **Behavior**: Stays active indefinitely (no auto-transition)
  - **Purpose**: Provides visual confirmation before unplugging device
  - **Distinction**: Blue/purple color distinguishes from red ERROR state
  - **Trigger**: `controller.trigger_error_test()` public API method
  - **Implementation**: `state_error_test.cpp` with breathing effect

- **Controller Enhancements**:
  - Added `trigger_error_test()` public API method
  - Updated `get_state_name()` to include "ERROR_TEST"
  - New state handler in `state_error_test.cpp`

### Configuration
- Added to `plantOS.yaml` lines 371-405:
  - `persistent_state_manager` (id: psm)
  - `psm_checker` (id: psm_test)
  - Comprehensive documentation in YAML comments

### Use Cases
- Chemical dosing operations (acid/base pumps)
- Watering valve control (prevent overflow)
- Long-running calibration sequences
- Any critical operation that could cause damage if interrupted

### Testing Instructions
1. **First Boot**: Wait 10 seconds, then check PLANTOS SYSTEM STATUS REPORT for alert:
   - Alert: "⚡ PLEASE UNPLUG DEVICE NOW!"
   - Blue/purple pulsing LED indicates ERROR_TEST state
2. **Unplug**: Remove power while LED is pulsing blue/purple
3. **Replug**: Wait 10 seconds after boot, then check PLANTOS SYSTEM STATUS REPORT for alert:
   - Alert: "✓ PSM TEST SUCCESSFUL! Event recovered from NVS after reboot"
   - Includes event age in seconds
4. **Verify**: Event was successfully logged, survived power cycle, and was cleared

### Technical Details
- Uses ESPHome `global_preferences->make_preference<T>()` API
- NVS key hash: `fnv1_hash("critical_event")`
- Event structure size: 44 bytes (32 + 8 + 4)
- Requires NTP sync for accurate event age calculation
- Falls back to millis() if NTP not synchronized
- PSMChecker 10-second delay implemented via boot_time_ tracking in loop()
- PSM alerts integrated with CentralStatusLogger for unified status reporting
- Status report interval: 30 seconds (alerts visible in periodic status logs)

---

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
