# ActuatorSafetyGate - ESPHome Integration Guide

This guide shows how to integrate the ActuatorSafetyGate into your PlantOS/ESPHome project.

## Integration Methods

There are three ways to integrate the ActuatorSafetyGate:

1. **Lambda-based** (Quick, no custom component needed)
2. **Custom Component** (Full ESPHome integration with YAML config)
3. **Direct C++ Integration** (Within existing custom components)

---

## Method 1: Lambda-Based Integration (Recommended for Quick Start)

This method uses ESPHome's lambda features to integrate the safety gate without creating a custom component.

### Step 1: Add to plantOS.yaml

```yaml
external_components:
  - source: components

# Create global instance
globals:
  - id: actuator_safety
    type: ActuatorSafetyGate*
    restore_value: no
    initial_value: 'new ActuatorSafetyGate()'

# Include the header
esphome:
  includes:
    - components/actuator_safety_gate/ActuatorSafetyGate.h
    - components/actuator_safety_gate/ActuatorSafetyGate.cpp

  # Initialize on boot
  on_boot:
    priority: 600
    then:
      - lambda: |-
          // Initialize safety gate
          id(actuator_safety)->begin();

          // Configure maximum durations for critical actuators
          id(actuator_safety)->setMaxDuration("AcidPump", 30);
          id(actuator_safety)->setMaxDuration("WaterValve", 120);
          id(actuator_safety)->setMaxDuration("NutrientPump", 15);

          ESP_LOGI("safety", "ActuatorSafetyGate initialized");

# Periodic monitoring
interval:
  - interval: 1s
    then:
      - lambda: |-
          id(actuator_safety)->loop();
```

### Step 2: Use in Buttons/Switches

```yaml
button:
  - platform: template
    name: "Dose Acid (5s)"
    on_press:
      - lambda: |-
          if (id(actuator_safety)->executeCommand("AcidPump", true, 5)) {
              ESP_LOGI("button", "Acid pump activated");
              // Turn on actual pump here
              // id(acid_pump_relay).turn_on();
          } else {
              ESP_LOGE("button", "Acid pump command rejected by safety gate");
          }

  - platform: template
    name: "Stop Acid Pump"
    on_press:
      - lambda: |-
          if (id(actuator_safety)->executeCommand("AcidPump", false)) {
              ESP_LOGI("button", "Acid pump stopped");
              // Turn off actual pump
              // id(acid_pump_relay).turn_off();
          }

  - platform: template
    name: "Water Zone 1 (3min)"
    on_press:
      - lambda: |-
          if (id(actuator_safety)->executeCommand("Zone1Valve", true, 180)) {
              ESP_LOGI("button", "Zone 1 watering started");
              // Open valve
              // id(zone1_valve).turn_on();

              // Schedule auto-shutoff after 3 minutes
              // (ESPHome will handle this with delayed_off)
          }
```

### Step 3: Monitor Runtime and Violations

```yaml
sensor:
  - platform: template
    name: "Acid Pump Runtime"
    id: acid_pump_runtime
    unit_of_measurement: "s"
    update_interval: 1s
    lambda: |-
      return id(actuator_safety)->getRuntime("AcidPump");

  - platform: template
    name: "Water Valve Runtime"
    id: water_valve_runtime
    unit_of_measurement: "s"
    update_interval: 1s
    lambda: |-
      return id(actuator_safety)->getRuntime("WaterValve");

binary_sensor:
  - platform: template
    name: "Acid Pump Violation"
    lambda: |-
      return id(actuator_safety)->isViolatingDuration("AcidPump");
```

---

## Method 2: Custom ESPHome Component (Full Integration)

Create a proper ESPHome component with YAML configuration support.

### Step 1: Create Python Integration

Create `components/actuator_safety_gate/__init__.py`:

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# Define namespace
actuator_safety_gate_ns = cg.esphome_ns.namespace('actuator_safety_gate')
ActuatorSafetyGate = actuator_safety_gate_ns.class_('ActuatorSafetyGate', cg.Component)

# Configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ActuatorSafetyGate),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
```

### Step 2: Update C++ Files

Modify `ActuatorSafetyGate.h` to inherit from ESPHome Component:

```cpp
#include "esphome/core/component.h"

namespace esphome {
namespace actuator_safety_gate {

class ActuatorSafetyGate : public Component {
public:
    // ... existing methods ...

    void setup() override {
        begin();
    }

    void loop() override {
        // Existing loop() logic
    }

    // Keep all other methods the same
};

} // namespace actuator_safety_gate
} // namespace esphome
```

### Step 3: Use in YAML

```yaml
actuator_safety_gate:
  id: safety_gate

# Access via lambdas
button:
  - platform: template
    name: "Dose Acid"
    on_press:
      - lambda: |-
          if (id(safety_gate).executeCommand("AcidPump", true, 5)) {
              // Turn on pump
          }
```

---

## Method 3: Direct C++ Integration

Use the safety gate within an existing custom component.

### Example: Integrate with Controller Component

Modify `components/controller/controller.h`:

```cpp
#include "esphome/core/component.h"
#include "components/actuator_safety_gate/ActuatorSafetyGate.h"

class Controller : public Component {
public:
    void setup() override {
        // Initialize safety gate
        safety_gate_.begin();
        safety_gate_.setMaxDuration("Pump", 30);
    }

    void loop() override {
        // Monitor safety gate
        safety_gate_.loop();

        // Your controller logic here
        if (needToDosePump()) {
            activatePump(5);  // Request 5 seconds
        }
    }

    void activatePump(int seconds) {
        if (safety_gate_.executeCommand("Pump", true, seconds)) {
            // Turn on physical pump
            digitalWrite(PUMP_PIN, HIGH);
        }
    }

    void deactivatePump() {
        if (safety_gate_.executeCommand("Pump", false)) {
            digitalWrite(PUMP_PIN, LOW);
        }
    }

private:
    ActuatorSafetyGate safety_gate_;
};
```

---

## Complete Example: pH Control System

Here's a complete example integrating the safety gate with a pH control system:

```yaml
# plantOS.yaml

external_components:
  - source: components

# Safety gate global instance
globals:
  - id: actuator_safety
    type: ActuatorSafetyGate*
    restore_value: no
    initial_value: 'new ActuatorSafetyGate()'

esphome:
  includes:
    - components/actuator_safety_gate/ActuatorSafetyGate.h
    - components/actuator_safety_gate/ActuatorSafetyGate.cpp

  on_boot:
    then:
      - lambda: |-
          id(actuator_safety)->begin();
          id(actuator_safety)->setMaxDuration("AcidPump", 10);
          id(actuator_safety)->setMaxDuration("BasePump", 10);

# Monitor safety gate
interval:
  - interval: 1s
    then:
      - lambda: |-
          id(actuator_safety)->loop();

# pH Sensor (example)
sensor:
  - platform: template
    name: "pH Level"
    id: ph_sensor
    unit_of_measurement: "pH"
    accuracy_decimals: 2
    # In reality, this would be a real pH sensor
    lambda: |-
      return 7.2;  // Simulated value

  - platform: template
    name: "Acid Pump Runtime"
    unit_of_measurement: "s"
    update_interval: 1s
    lambda: |-
      return id(actuator_safety)->getRuntime("AcidPump");

# pH Control Logic
interval:
  - interval: 5s
    then:
      - lambda: |-
          float current_ph = id(ph_sensor).state;
          float target_ph = 6.5;

          // Need to lower pH (dose acid)
          if (current_ph > target_ph + 0.2) {
              if (id(actuator_safety)->executeCommand("AcidPump", true, 2)) {
                  ESP_LOGI("ph_control", "Dosing acid for 2 seconds");
                  // id(acid_pump_relay).turn_on();

                  // Schedule shutoff after 2 seconds
                  // (Use ESPHome delayed_off or set_timeout)
              }
          }

          // Need to raise pH (dose base)
          else if (current_ph < target_ph - 0.2) {
              if (id(actuator_safety)->executeCommand("BasePump", true, 2)) {
                  ESP_LOGI("ph_control", "Dosing base for 2 seconds");
                  // id(base_pump_relay).turn_on();
              }
          }

# Manual control buttons
button:
  - platform: template
    name: "Manual Acid Dose (5s)"
    on_press:
      - lambda: |-
          if (id(actuator_safety)->executeCommand("AcidPump", true, 5)) {
              ESP_LOGI("manual", "Manual acid dose started");
              // Turn on pump
          }

  - platform: template
    name: "Emergency Stop All"
    on_press:
      - lambda: |-
          ESP_LOGW("emergency", "EMERGENCY STOP TRIGGERED");
          id(actuator_safety)->forceReset("AcidPump", false);
          id(actuator_safety)->forceReset("BasePump", false);
          // Turn off all physical hardware
          // id(acid_pump_relay).turn_off();
          // id(base_pump_relay).turn_off();

# Violation monitoring
binary_sensor:
  - platform: template
    name: "Acid Pump Violation Alert"
    lambda: |-
      return id(actuator_safety)->isViolatingDuration("AcidPump");
    on_press:
      - logger.log:
          format: "WARNING: Acid pump exceeded maximum duration!"
          level: WARN
      # Could trigger emergency shutoff here
```

---

## Best Practices

1. **Always initialize in on_boot**: Ensure the safety gate is initialized before any actuators are used

2. **Call loop() regularly**: Use a 1-second interval to monitor for violations

3. **Configure max durations early**: Set limits during initialization, not during runtime

4. **Handle rejections gracefully**: Always check the return value of `executeCommand()`

5. **Log application context**: When commands are rejected, log additional context about what you were trying to do

6. **Monitor violations**: Set up binary sensors or alerts for duration violations

7. **Implement auto-shutoff**: Use ESPHome's `delayed_off` or `set_timeout` to automatically turn off actuators after their runtime expires

8. **Test safety limits**: During development, test that limits are enforced correctly

---

## Troubleshooting

### Commands Always Rejected

Check if you're requesting the same state twice (debouncing). Turn the actuator OFF before trying to turn it ON again.

### Duration Violations Not Detected

Ensure `loop()` is being called regularly (at least once per second).

### Compilation Errors

Make sure the header files are included correctly:
```yaml
esphome:
  includes:
    - components/actuator_safety_gate/ActuatorSafetyGate.h
    - components/actuator_safety_gate/ActuatorSafetyGate.cpp
```

### Runtime Not Tracking

The runtime counter only starts when `executeCommand(..., true, duration)` is called with state=true.

---

## Advanced: Custom Safety Rules

You can extend the ActuatorSafetyGate to add custom safety rules:

```cpp
// In your custom component
class CustomSafetyGate : public ActuatorSafetyGate {
public:
    bool executeCommand(const char* actuatorID, bool targetState, int maxDurationSeconds = 0) override {
        // Custom rule: Never allow acid pump and base pump to run simultaneously
        if (strcmp(actuatorID, "AcidPump") == 0 && targetState == true) {
            if (getState("BasePump")) {
                ESP_LOGW("custom_safety", "REJECTED: Cannot run acid pump while base pump is active");
                return false;
            }
        }

        // Call parent implementation for standard checks
        return ActuatorSafetyGate::executeCommand(actuatorID, targetState, maxDurationSeconds);
    }
};
```

---

## Testing Checklist

- [ ] Safety gate initializes successfully
- [ ] Max durations are configured correctly
- [ ] Debouncing prevents duplicate commands
- [ ] Duration limits are enforced
- [ ] Violations are detected and logged
- [ ] Runtime tracking is accurate
- [ ] Emergency shutoff works
- [ ] Commands log approval/rejection reasons

---

## Support

For questions or issues with the ActuatorSafetyGate, check:
- README.md for API documentation
- example_usage.cpp for usage patterns
- PlantOS project documentation
