# Integration Guide: CentralStatusLogger with PlantOS

This guide explains how to integrate the CentralStatusLogger into your existing PlantOS ESPHome project.

## Quick Integration Checklist

- [ ] Add logger instance to controller component
- [ ] Initialize logger in controller setup()
- [ ] Update logger from sensor callbacks
- [ ] Update logger on state transitions
- [ ] Call logStatus() every 30 seconds
- [ ] Trigger alerts on error conditions

## Method 1: Direct Integration into Controller Component

### Step 1: Modify `controller.h`

```cpp
#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/light/light_state.h"
#include "../central_status_logger/CentralStatusLogger.h"  // Add this

namespace esphome {
namespace plantos_controller {

class PlantOSController : public Component {
 public:
  void setup() override;
  void loop() override;

  void set_sensor_source(sensor::Sensor *sensor);
  void set_light_target(light::LightState *light);

  // Add public method to access logger for external components
  CentralStatusLogger* get_logger() { return &status_logger_; }

 private:
  // Existing state machine members...
  enum State { STATE_INIT, STATE_CALIBRATION, STATE_READY, STATE_ERROR };
  State current_state_;
  State (PlantOSController::*state_handler_)();

  sensor::Sensor *sensor_source_;
  light::LightState *light_target_;
  float last_sensor_value_;

  // State handlers...
  State state_init();
  State state_calibration();
  State state_ready();
  State state_error();

  void sensor_callback_(float value);

  // Add status logger
  CentralStatusLogger status_logger_;
  unsigned long last_status_log_;
};

}  // namespace plantos_controller
}  // namespace esphome

#endif
```

### Step 2: Modify `controller.cpp`

```cpp
#include "controller.h"
#include "esphome/core/log.h"

namespace esphome {
namespace plantos_controller {

static const char *TAG = "plantos.controller";

void PlantOSController::setup() {
  ESP_LOGI(TAG, "Setting up PlantOS Controller...");

  // Initialize status logger
  status_logger_.begin();
  last_status_log_ = millis();

  // Update with initial network info
  // Note: In ESPHome, you'd get this from WiFi component
  status_logger_.updateIP("0.0.0.0");  // Will be updated once WiFi connects
  status_logger_.updateWebServerStatus(false, false);
  status_logger_.updateStatus(0.0, "INIT");

  // Existing setup code...
  current_state_ = STATE_INIT;
  state_handler_ = &PlantOSController::state_init;
  last_sensor_value_ = 0.0f;

  // Register sensor callback
  if (sensor_source_ != nullptr) {
    sensor_source_->add_on_state_callback([this](float value) {
      this->sensor_callback_(value);
    });
  }

  ESP_LOGI(TAG, "PlantOS Controller initialized");
}

void PlantOSController::loop() {
  // Execute current state handler
  State next_state = (this->*state_handler_)();

  // Handle state transitions
  if (next_state != current_state_) {
    ESP_LOGI(TAG, "State transition: %d -> %d", current_state_, next_state);

    // Update status logger on state change
    String state_name;
    switch (next_state) {
      case STATE_INIT: state_name = "INIT"; break;
      case STATE_CALIBRATION: state_name = "CALIBRATION"; break;
      case STATE_READY: state_name = "READY"; break;
      case STATE_ERROR: state_name = "ERROR"; break;
    }
    status_logger_.updateStatus(last_sensor_value_, state_name);

    current_state_ = next_state;

    // Update function pointer
    switch (current_state_) {
      case STATE_INIT: state_handler_ = &PlantOSController::state_init; break;
      case STATE_CALIBRATION: state_handler_ = &PlantOSController::state_calibration; break;
      case STATE_READY: state_handler_ = &PlantOSController::state_ready; break;
      case STATE_ERROR: state_handler_ = &PlantOSController::state_error; break;
    }
  }

  // Periodic status logging (every 30 seconds)
  if (millis() - last_status_log_ >= 30000) {
    status_logger_.logStatus();
    last_status_log_ = millis();
  }
}

void PlantOSController::sensor_callback_(float value) {
  last_sensor_value_ = value;

  // Update logger with new sensor value
  String current_state_name;
  switch (current_state_) {
    case STATE_INIT: current_state_name = "INIT"; break;
    case STATE_CALIBRATION: current_state_name = "CALIBRATION"; break;
    case STATE_READY: current_state_name = "READY"; break;
    case STATE_ERROR: current_state_name = "ERROR"; break;
  }
  status_logger_.updateStatus(value, current_state_name);

  ESP_LOGD(TAG, "Sensor value: %.2f", value);
}

// ... existing state implementations ...

}  // namespace plantos_controller
}  // namespace esphome
```

### Step 3: Update State Implementations with Alerts

**In `state_ready.cpp`:**

```cpp
PlantOSController::State PlantOSController::state_ready() {
  // Existing breathing animation code...

  // Check for error condition
  if (last_sensor_value_ > 90.0f) {
    ESP_LOGW(TAG, "Sensor value too high! Transitioning to ERROR state");

    // Trigger critical alert
    status_logger_.updateAlertStatus(
      "SENSOR_HIGH",
      "Sensor value exceeds threshold: " + String(last_sensor_value_, 2) + " > 90.0"
    );

    return STATE_ERROR;
  }

  // Clear any sensor alerts if we're in normal range
  if (last_sensor_value_ <= 90.0f) {
    status_logger_.clearAlert("SENSOR_HIGH");
  }

  return STATE_READY;
}
```

**In `state_error.cpp`:**

```cpp
PlantOSController::State PlantOSController::state_error() {
  static uint32_t error_start_time = millis();

  // Fast red flashing
  auto call = light_target_->turn_on();
  call.set_brightness(1.0f);

  uint32_t blink_phase = (millis() / 200) % 2;
  if (blink_phase == 0) {
    call.set_rgb(1.0f, 0.0f, 0.0f);
  } else {
    call.set_rgb(0.0f, 0.0f, 0.0f);
  }
  call.perform();

  // After 5 seconds, clear alerts and return to init
  if (millis() - error_start_time >= 5000) {
    ESP_LOGI(TAG, "Error state timeout, returning to INIT");

    // Clear all alerts before leaving error state
    status_logger_.clearAllAlerts();

    error_start_time = millis();
    return STATE_INIT;
  }

  return STATE_ERROR;
}
```

## Method 2: Standalone Logger Component

Create a separate ESPHome component that manages the logger independently.

### Create `components/status_logger/status_logger.h`:

```cpp
#pragma once

#include "esphome/core/component.h"
#include "../central_status_logger/CentralStatusLogger.h"

namespace esphome {
namespace status_logger {

class StatusLoggerComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // Allow other components to update the logger
  void update_sensor_value(const std::string& name, float value);
  void update_routine(const std::string& routine);
  void trigger_alert(const std::string& type, const std::string& reason);
  void clear_alert(const std::string& type);

  CentralStatusLogger* get_logger() { return &logger_; }

 private:
  CentralStatusLogger logger_;
  unsigned long last_log_time_;
};

}  // namespace status_logger
}  // namespace esphome
```

### Create `components/status_logger/__init__.py`:

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome import automation

CODEBASE_NAME = "status_logger"
DEPENDENCIES = []

status_logger_ns = cg.esphome_ns.namespace('status_logger')
StatusLoggerComponent = status_logger_ns.class_('StatusLoggerComponent', cg.Component)

# Actions
UpdateSensorAction = status_logger_ns.class_('UpdateSensorAction', automation.Action)
TriggerAlertAction = status_logger_ns.class_('TriggerAlertAction', automation.Action)
ClearAlertAction = status_logger_ns.class_('ClearAlertAction', automation.Action)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(StatusLoggerComponent),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
```

### Use in `plantOS.yaml`:

```yaml
# Add the status logger component
status_logger:
  id: system_logger

# Update from automations
automation:
  - interval: 1s
    then:
      - lambda: |-
          // Update IP address from WiFi
          if (wifi::global_wifi_component->is_connected()) {
            id(system_logger).get_logger()->updateIP(
              wifi::global_wifi_component->get_ip_address().str().c_str()
            );
          }

          // Update web server status
          #ifdef USE_WEBSERVER
          id(system_logger).get_logger()->updateWebServerStatus(true, false);
          #endif
```

## Accessing Logger from Lambda Functions

You can also interact with the logger directly from ESPHome lambda functions:

```yaml
sensor:
  - platform: sensor_dummy
    id: sensor_dummy_id
    name: "Dummy Sensor"
    update_interval: 1s
    on_value:
      then:
        - lambda: |-
            // Update logger with sensor value
            id(my_logic_controller).get_logger()->updateStatus(x, "MONITORING");

            // Trigger alert if value too high
            if (x > 90.0) {
              id(my_logic_controller).get_logger()->updateAlertStatus(
                "SENSOR_CRITICAL",
                "Sensor value: " + to_string(x) + " exceeds 90.0"
              );
            } else {
              id(my_logic_controller).get_logger()->clearAlert("SENSOR_CRITICAL");
            }
```

## Monitoring Web Server Status

To get actual web server status in ESPHome:

```yaml
web_server:
  port: 80
  id: plantos_webserver

# In automation or lambda:
automation:
  - interval: 5s
    then:
      - lambda: |-
          #ifdef USE_WEBSERVER
          bool server_online = true;  // Web server component is always running in ESPHome
          bool client_connected = false;  // ESPHome doesn't expose this directly

          id(system_logger).get_logger()->updateWebServerStatus(
            server_online,
            client_connected
          );
          #endif
```

## Getting NTP-Synchronized Time

The logger uses `time(NULL)` which requires NTP synchronization. Add to your `plantOS.yaml`:

```yaml
time:
  - platform: sntp
    id: sntp_time
    timezone: America/New_York
    servers:
      - 0.pool.ntp.org
      - 1.pool.ntp.org
      - 2.pool.ntp.org
```

## Testing the Integration

1. **Build and flash:**
   ```bash
   task build
   task flash
   ```

2. **Monitor logs:**
   ```bash
   esphome logs plantOS.yaml
   ```

3. **Expected output every 30 seconds:**
   - System time (once NTP syncs)
   - Current IP address
   - Web server status
   - Sensor values
   - Active routine/state
   - Any critical alerts

## Alert Examples for PlantOS

Here are some realistic alerts you might implement:

```cpp
// pH out of range
if (ph < 5.5 || ph > 8.5) {
  status_logger_.updateAlertStatus("PH_RANGE",
    "pH " + String(ph, 2) + " outside safe range (5.5-8.5)");
}

// Water level critical
if (water_level < 10.0) {
  status_logger_.updateAlertStatus("WATER_LOW",
    "Reservoir water level critically low: " + String(water_level, 1) + "%");
}

// Spill detection
if (floor_sensor_wet) {
  status_logger_.updateAlertStatus("SPILL",
    "Water detected on floor sensor");
}

// Temperature alerts
if (temperature > 30.0) {
  status_logger_.updateAlertStatus("TEMP_HIGH",
    "Temperature " + String(temperature, 1) + "°C exceeds 30°C");
}

// Pump failure
if (pump_running && flow_rate < 0.1) {
  status_logger_.updateAlertStatus("PUMP_FAILURE",
    "Pump running but no flow detected");
}
```

## Best Practices

1. **Alert Naming**: Use consistent, descriptive alert type names (all caps, underscores)
2. **Alert Reasons**: Include actual values and thresholds in alert messages
3. **Clear Alerts**: Always clear alerts when conditions return to normal
4. **State Transitions**: Update logger whenever state machine transitions
5. **Network Updates**: Update IP and web server status when WiFi state changes
6. **Sensor Updates**: Update logger on every sensor callback or at reasonable intervals

## Troubleshooting

**Q: Time shows "NTP Not Synchronized"**
- Ensure you have a `time:` platform configured in your YAML
- Check WiFi connection
- Wait 30-60 seconds after boot for NTP sync

**Q: IP shows "0.0.0.0"**
- Update the IP from WiFi component after connection
- Use lambda in WiFi `on_connect` trigger

**Q: Alerts not clearing**
- Ensure you call `clearAlert()` with the exact same type string
- Or use `clearAllAlerts()` to reset all alerts

**Q: High memory usage**
- Each alert takes ~100 bytes
- Limit simultaneous alerts to <10
- Clear old alerts promptly
