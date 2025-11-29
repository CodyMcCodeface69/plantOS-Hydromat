# CentralStatusLogger

A unified logging and status display system for ESP32-based PlantOS, designed to manage and report all critical system variables with support for multiple simultaneous alerts.

## Features

- **Structured Status Logging**: Comprehensive system state reporting every 30 seconds
- **Multiple Simultaneous Alerts**: Support for tracking multiple critical alerts at once (e.g., spill + pH + temperature)
- **Network Status Integration**: IP address and web server status monitoring
- **Non-blocking Periodic Logging**: Uses `millis()` for precise 30-second intervals
- **Formatted Time Display**: NTP-synchronized timestamps in human-readable format
- **Visual Alert System**: Critical alerts displayed with high-visibility banner formatting

## Architecture

### Core Components

1. **Alert Management System**
   - Uses `std::vector<Alert>` to store multiple simultaneous alerts
   - Each alert has: type, reason, and timestamp
   - Supports add, update, clear individual, and clear all operations

2. **System State Tracking**
   - IP address (from WiFi/network component)
   - Web server status (online/offline, client connected)
   - Sensor data (pH, temperature, etc.)
   - Active routine/state machine state

3. **Formatted Output**
   - Structured sections: Network, Sensors, System State, Alerts
   - Critical alerts displayed prominently after timestamp
   - Alert age tracking (how long alert has been active)

## API Reference

### Initialization

```cpp
CentralStatusLogger statusLogger;

void setup() {
    Serial.begin(115200);
    statusLogger.begin();
}
```

### Updating System Status

```cpp
// Update sensor readings and routine
statusLogger.updateStatus(7.2, "DOSING_ACID");

// Update network information
statusLogger.updateIP("192.168.1.100");

// Update web server status
statusLogger.updateWebServerStatus(true, true);  // online, client connected
```

### Alert Management

```cpp
// Add or update an alert
statusLogger.updateAlertStatus("SPILL", "Water detected on floor sensor #2");
statusLogger.updateAlertStatus("PH_CRITICAL", "pH level: 9.5 (max: 8.5)");
statusLogger.updateAlertStatus("TEMPERATURE", "Reservoir temp: 32°C");

// Clear specific alert
statusLogger.clearAlert("SPILL");

// Clear all alerts
statusLogger.clearAllAlerts();

// Check alert status
if (statusLogger.hasActiveAlerts()) {
    int count = statusLogger.getAlertCount();
    Serial.printf("Warning: %d active alerts\n", count);
}
```

### Logging

```cpp
// Print complete status report (call every 30 seconds)
statusLogger.logStatus();
```

## Example Output

### Normal Operation (No Alerts)

```
================================================================================
  PLANTOS SYSTEM STATUS REPORT
================================================================================
System Time: 2025-11-29 14:23:45

--- NETWORK STATUS ---
  IP Address: 192.168.1.100
  Web Server: ONLINE (No Clients)

--- SENSOR DATA ---
  Filtered pH: 7.23

--- SYSTEM STATE ---
  Active Routine: IDLE

--- ALERT STATUS ---
  Status: ALL CLEAR
================================================================================
```

### Critical Alert State (Multiple Alerts)

```
================================================================================
  PLANTOS SYSTEM STATUS REPORT
================================================================================
System Time: 2025-11-29 14:24:15

********************************************************************************
********************************************************************************
***                     CRITICAL ALERTS ACTIVE                       ***
********************************************************************************
********************************************************************************

*** ALERT #1: SPILL
*** Reason: Water detected on floor sensor #2
*** Active for: 15 seconds

*** ALERT #2: PH_CRITICAL
*** Reason: pH level outside safe range: 9.43
*** Active for: 8 seconds

*** ALERT #3: TEMPERATURE
*** Reason: Reservoir temperature too high: 32.5°C
*** Active for: 2 seconds

********************************************************************************
********************************************************************************

--- NETWORK STATUS ---
  IP Address: 192.168.1.100
  Web Server: ONLINE (Client Connected)

--- SENSOR DATA ---
  Filtered pH: 9.43

--- SYSTEM STATE ---
  Active Routine: DOSING_ACID

--- ALERT STATUS ---
  Active Alerts: 3
================================================================================
```

## Integration with PlantOS/ESPHome

### Option 1: Arduino Framework (Standalone)

If using this as a standalone Arduino sketch:

```cpp
#include "CentralStatusLogger.h"

CentralStatusLogger logger;
unsigned long lastLog = 0;

void setup() {
    Serial.begin(115200);
    logger.begin();

    // Initialize WiFi, sensors, etc.
    logger.updateIP(WiFi.localIP().toString());
}

void loop() {
    // Update from sensors
    logger.updateStatus(readPH(), getCurrentRoutine());

    // 30-second periodic logging
    if (millis() - lastLog >= 30000) {
        logger.logStatus();
        lastLog = millis();
    }
}
```

### Option 2: ESPHome Custom Component

To integrate with ESPHome, create a custom component wrapper:

**Python side (`__init__.py`):**
```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

central_logger_ns = cg.esphome_ns.namespace('central_logger')
CentralStatusLoggerComponent = central_logger_ns.class_('CentralStatusLoggerComponent', cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(CentralStatusLoggerComponent)
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
```

**C++ side (component wrapper):**
```cpp
#include "CentralStatusLogger.h"
#include "esphome/core/component.h"

namespace esphome {
namespace central_logger {

class CentralStatusLoggerComponent : public Component {
 public:
  void setup() override {
    logger_.begin();
    // Get IP from WiFi component
    // Get web server status, etc.
  }

  void loop() override {
    if (millis() - last_log_ >= 30000) {
      logger_.logStatus();
      last_log_ = millis();
    }
  }

  CentralStatusLogger* get_logger() { return &logger_; }

 private:
  CentralStatusLogger logger_;
  unsigned long last_log_ = 0;
};

}  // namespace central_logger
}  // namespace esphome
```

## Design Decisions

1. **Multiple Alert Support**: Changed from single bool + String to vector-based system to handle realistic scenarios where multiple critical conditions occur simultaneously

2. **Alert Structure**: Each alert has type (identifier), reason (details), and timestamp (for tracking alert age)

3. **Non-blocking Timing**: Uses `millis()` comparison instead of `delay()` to maintain responsive system behavior

4. **Visual Hierarchy**: Critical alerts displayed immediately after timestamp with high-visibility formatting to ensure operator attention

5. **Extensibility**: Easy to add new status fields (temperature, water level, etc.) by adding member variables and updating `logStatus()`

## File Structure

```
components/central_status_logger/
├── CentralStatusLogger.h      # Class declaration
├── CentralStatusLogger.cpp    # Implementation
├── example_usage.cpp          # Complete working example
└── README.md                  # This file
```

## Testing the Example

The `example_usage.cpp` demonstrates a complete simulation timeline:

- **0s**: System initialization, first status log
- **10s**: Water spill alert triggered
- **20s**: pH critical alert added (2 simultaneous alerts)
- **30s**: Second periodic status log (with 2 alerts visible)
- **35s**: Temperature alert added (3 simultaneous alerts)
- **50s**: Spill alert cleared (2 alerts remain)
- **60s**: Third periodic status log (with 2 alerts)
- **70s**: All alerts cleared
- **80s**: Web client connects
- **90s**: Fourth periodic status log (all clear, client connected)
- **95s**: Web client disconnects

## Future Enhancements

- Alert priority levels (INFO, WARNING, CRITICAL)
- Alert filtering by priority
- Alert history/log file
- Remote notification support (MQTT, email, etc.)
- Configurable log intervals
- JSON output format option
- Web dashboard integration
