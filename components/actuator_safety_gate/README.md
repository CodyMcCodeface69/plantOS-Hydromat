# ActuatorSafetyGate

## Overview

**ActuatorSafetyGate** is a centralized safety layer for controlling all system actuators (pumps, valves, motors, etc.) in the PlantOS system. It enforces safety rules, prevents unnecessary operations, and logs violations before execution.

## Key Features

### 1. **Debouncing Protection**
Prevents redundant commands by tracking the last requested state for each actuator. If you request the same state twice, the second command is rejected to prevent unnecessary pin toggles and repeated operations.

```cpp
safetyGate.executeCommand("AcidPump", true, 5);   // APPROVED
safetyGate.executeCommand("AcidPump", true, 5);   // REJECTED (debouncing)
safetyGate.executeCommand("AcidPump", false);     // APPROVED (different state)
```

### 2. **Maximum Duration Enforcement**
For critical actuators like pumps, enforces maximum runtime limits to prevent overruns, spills, and hardware damage.

```cpp
// Configure global max duration
safetyGate.setMaxDuration("AcidPump", 30);  // Never allow > 30 seconds

// Request shorter duration (approved)
safetyGate.executeCommand("AcidPump", true, 10);  // APPROVED (10 < 30)

// Request longer duration (rejected)
safetyGate.executeCommand("AcidPump", true, 60);  // REJECTED (60 > 30)
```

### 3. **Safety Violation Logging**
Every rejection is logged with clear messages indicating the reason:

```
[W][actuator.safety:XXX]: REJECTED: AcidPump - Max duration violation - requested 60s exceeds limit 30s
[W][actuator.safety:XXX]: REJECTED: WaterValve - Debouncing - state already requested
```

### 4. **Runtime Tracking**
Monitors how long actuators have been running and can detect if they exceed their configured limits.

```cpp
// Get current runtime
uint32_t runtime = safetyGate.getRuntime("AcidPump");
ESP_LOGI(TAG, "Pump has been running for %u seconds", runtime);

// Check if violating duration
if (safetyGate.isViolatingDuration("AcidPump")) {
    ESP_LOGW(TAG, "EMERGENCY: Pump exceeded max duration!");
    // Trigger emergency shutoff
}
```

### 5. **Centralized Control**
Single point of control for all actuators, making it easy to add global safety rules or emergency shutoff.

## Architecture

```
┌─────────────────────────────────────────────────┐
│         Application Logic                       │
│  (pH Control, Watering, Dosing Routines)       │
└────────────────┬────────────────────────────────┘
                 │
                 │ executeCommand()
                 ▼
┌─────────────────────────────────────────────────┐
│      ActuatorSafetyGate (Safety Layer)         │
│                                                  │
│  ✓ Debouncing Check                            │
│  ✓ Duration Limit Check                        │
│  ✓ State Tracking                              │
│  ✓ Violation Logging                           │
└────────────────┬────────────────────────────────┘
                 │
                 │ approved/rejected
                 ▼
┌─────────────────────────────────────────────────┐
│         Hardware Control                        │
│  (GPIO pins, relays, pump drivers)             │
└─────────────────────────────────────────────────┘
```

## Usage

### Basic Setup

```cpp
#include "ActuatorSafetyGate.h"

// Create global instance
ActuatorSafetyGate safetyGate;

void setup() {
    // Initialize the safety gate
    safetyGate.begin();

    // Configure maximum durations for critical actuators
    safetyGate.setMaxDuration("AcidPump", 30);      // 30 seconds max
    safetyGate.setMaxDuration("WaterValve", 120);   // 2 minutes max
    safetyGate.setMaxDuration("NutrientPump", 15);  // 15 seconds max
}

void loop() {
    // Periodically check for duration violations
    safetyGate.loop();

    // Your application logic here...
}
```

### Controlling Actuators

```cpp
// Example: Acid dosing routine
void doseAcid(int durationSeconds) {
    // Request to turn on acid pump
    if (safetyGate.executeCommand("AcidPump", true, durationSeconds)) {
        // Command approved - turn on pump
        digitalWrite(ACID_PUMP_PIN, HIGH);
        ESP_LOGI(TAG, "Acid pump activated for %d seconds", durationSeconds);
    } else {
        // Command rejected - safety gate logged the reason
        ESP_LOGE(TAG, "Failed to activate acid pump - check logs for reason");
    }
}

// Example: Stopping the pump
void stopAcidPump() {
    if (safetyGate.executeCommand("AcidPump", false)) {
        // Command approved - turn off pump
        digitalWrite(ACID_PUMP_PIN, LOW);
        ESP_LOGI(TAG, "Acid pump stopped");
    }
}
```

### Advanced: State Monitoring

```cpp
// Check current state
if (safetyGate.getState("AcidPump")) {
    ESP_LOGI(TAG, "Acid pump is currently ON");
}

// Get runtime
uint32_t runtime = safetyGate.getRuntime("AcidPump");
ESP_LOGI(TAG, "Pump has been running for %u seconds", runtime);

// Check for violations
if (safetyGate.isViolatingDuration("AcidPump")) {
    ESP_LOGW(TAG, "WARNING: Pump exceeded max duration!");
    // Trigger emergency shutoff
    digitalWrite(ACID_PUMP_PIN, LOW);
    safetyGate.forceReset("AcidPump", false);
}

// Get comprehensive stats
bool state;
uint32_t runtime, maxDuration;
if (safetyGate.getStats("AcidPump", state, runtime, maxDuration)) {
    ESP_LOGI(TAG, "Pump Stats - State: %s, Runtime: %us, Max: %us",
             state ? "ON" : "OFF", runtime, maxDuration);
}
```

### Emergency Override

```cpp
// Force reset (bypasses all safety checks)
// USE WITH CAUTION - only for emergency shutoff or manual override
void emergencyShutdown() {
    ESP_LOGW(TAG, "EMERGENCY SHUTDOWN TRIGGERED");

    // Force all actuators OFF
    safetyGate.forceReset("AcidPump", false);
    safetyGate.forceReset("WaterValve", false);
    safetyGate.forceReset("NutrientPump", false);

    // Turn off physical hardware
    digitalWrite(ACID_PUMP_PIN, LOW);
    digitalWrite(WATER_VALVE_PIN, LOW);
    digitalWrite(NUTRIENT_PUMP_PIN, LOW);
}
```

## Integration with ESPHome/PlantOS

### Option 1: Direct C++ Integration (in custom components)

```cpp
// In your custom component's header
#include "components/actuator_safety_gate/ActuatorSafetyGate.h"

class MyController : public Component {
private:
    ActuatorSafetyGate safetyGate_;
};

// In your component's implementation
void MyController::setup() {
    safetyGate_.begin();
    safetyGate_.setMaxDuration("Pump", 30);
}

void MyController::loop() {
    safetyGate_.loop();
}
```

### Option 2: Lambda Integration (in YAML)

```yaml
# Define global safety gate instance in lambda
globals:
  - id: safety_gate
    type: ActuatorSafetyGate*
    restore_value: no
    initial_value: 'new ActuatorSafetyGate()'

# Initialize in setup
esphome:
  on_boot:
    then:
      - lambda: |-
          id(safety_gate)->begin();
          id(safety_gate)->setMaxDuration("AcidPump", 30);
          id(safety_gate)->setMaxDuration("WaterValve", 120);

# Use in button/switch actions
button:
  - platform: template
    name: "Dose Acid"
    on_press:
      - lambda: |-
          if (id(safety_gate)->executeCommand("AcidPump", true, 5)) {
              // Turn on pump
              id(acid_pump_switch).turn_on();
          } else {
              ESP_LOGE("button", "Safety gate rejected acid pump command");
          }

# Periodic monitoring
interval:
  - interval: 1s
    then:
      - lambda: |-
          id(safety_gate)->loop();
```

## Safety Rules Summary

| Rule | Description | Example |
|------|-------------|---------|
| **Debouncing** | Requesting same state twice is rejected | `executeCommand("Pump", true)` twice → 2nd rejected |
| **Max Duration** | ON commands exceeding limits are rejected | Max=30s, request 60s → rejected |
| **Runtime Monitoring** | Actuators running beyond limits are flagged | Pump ON for 35s with max=30s → warning logged |
| **State Consistency** | All state changes are tracked and validated | Every approved command updates internal state |

## Logging

All safety gate operations are logged under the tag `actuator.safety`:

```
[I][actuator.safety:XXX]: ActuatorSafetyGate initialized
[I][actuator.safety:XXX]: Max duration set: AcidPump = 30 seconds
[I][actuator.safety:XXX]: APPROVED: AcidPump ON (max duration: 10 seconds)
[W][actuator.safety:XXX]: REJECTED: AcidPump - Debouncing - state already requested
[W][actuator.safety:XXX]: REJECTED: AcidPump - Max duration violation - requested 60s exceeds limit 30s
[W][actuator.safety:XXX]: DURATION VIOLATION: AcidPump has been ON for 35 seconds (limit: 30 seconds)
[I][actuator.safety:XXX]: APPROVED: AcidPump OFF (ran for 12 seconds)
```

## API Reference

### Core Methods

#### `void begin()`
Initialize the safety gate. Call once during setup.

#### `bool executeCommand(const char* actuatorID, bool targetState, int maxDurationSeconds = 0)`
Execute an actuator command with safety enforcement.
- Returns `true` if approved, `false` if rejected
- Logs reason for any rejection

#### `void setMaxDuration(const char* actuatorID, int maxSeconds)`
Set maximum allowed duration for an actuator.
- `maxSeconds = 0` removes the limit

#### `void loop()`
Periodic monitoring and violation detection. Call in main loop.

### State Query Methods

#### `bool getState(const char* actuatorID) const`
Get current tracked state (true=ON, false=OFF).

#### `uint32_t getRuntime(const char* actuatorID) const`
Get current runtime in seconds (0 if OFF).

#### `bool isViolatingDuration(const char* actuatorID) const`
Check if actuator is exceeding its max duration.

#### `bool getStats(...) const`
Get comprehensive statistics for an actuator.

### Emergency Methods

#### `void forceReset(const char* actuatorID, bool newState)`
Force reset state, bypassing all safety checks. **Use with caution!**

## Best Practices

1. **Always configure max durations** for critical actuators (pumps, valves)
2. **Call `loop()` regularly** to enable runtime monitoring
3. **Check return value** of `executeCommand()` and handle rejections
4. **Log application-level context** when commands are rejected
5. **Use `forceReset()` sparingly** - only for emergency shutoff
6. **Monitor duration violations** and implement automatic shutoff if needed

## Example: Complete pH Control System

```cpp
class pHController : public Component {
private:
    ActuatorSafetyGate safetyGate_;
    float currentPH_;
    float targetPH_;

public:
    void setup() override {
        safetyGate_.begin();
        safetyGate_.setMaxDuration("AcidPump", 10);   // Max 10 seconds per dose
        safetyGate_.setMaxDuration("BasePump", 10);
    }

    void loop() override {
        safetyGate_.loop();  // Monitor for violations

        // Check if pH adjustment needed
        if (currentPH_ > targetPH_ + 0.2) {
            // Need to lower pH - dose acid
            doseAcid(2);  // Request 2-second dose
        } else if (currentPH_ < targetPH_ - 0.2) {
            // Need to raise pH - dose base
            doseBase(2);
        }
    }

    void doseAcid(int seconds) {
        if (safetyGate_.executeCommand("AcidPump", true, seconds)) {
            digitalWrite(ACID_PUMP_PIN, HIGH);
            // Schedule auto-shutoff after 'seconds'
            set_timeout("acid_shutoff", seconds * 1000, [this]() {
                stopAcidPump();
            });
        } else {
            ESP_LOGE(TAG, "Acid dosing rejected by safety gate");
        }
    }

    void stopAcidPump() {
        if (safetyGate_.executeCommand("AcidPump", false)) {
            digitalWrite(ACID_PUMP_PIN, LOW);
        }
    }
};
```

## License

Part of the PlantOS project. See main project LICENSE.
