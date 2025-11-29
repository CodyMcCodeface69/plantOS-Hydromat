# DummyActuatorTrigger

## Overview

**DummyActuatorTrigger** is a test component designed to validate the functionality of the ActuatorSafetyGate. It runs automated test sequences that trigger various safety features including debouncing and maximum duration enforcement.

## Purpose

This component is used for:
- **Development Testing**: Validate safety gate functionality during development
- **Integration Testing**: Ensure safety features work correctly in the full system
- **Visual Feedback**: Provides LED indication of safety gate approvals/rejections
- **Automated Validation**: Runs tests periodically without manual intervention

## Test Sequences

The component cycles through three test sequences:

### Test 1: Debouncing Protection

**Purpose**: Validate that duplicate state requests are rejected

**Sequence**:
1. Request actuator ON → Expected: APPROVED
2. Request actuator ON again → Expected: REJECTED (debouncing)
3. Request actuator OFF → Expected: APPROVED
4. Request actuator ON → Expected: APPROVED (state changed, so new request allowed)

**Expected Log Output**:
```
[I][dummy.actuator:XXX]: TEST 1: DEBOUNCING PROTECTION
[I][actuator.safety:XXX]: APPROVED: TestActuator ON (max duration: 3 seconds)
[W][actuator.safety:XXX]: REJECTED: TestActuator - Debouncing - state already requested
[I][actuator.safety:XXX]: APPROVED: TestActuator OFF
```

### Test 2: Maximum Duration Enforcement

**Purpose**: Validate that duration limits are enforced

**Configuration**: TestActuator has 5-second maximum duration

**Sequence**:
1. Request ON for 3 seconds (within limit) → Expected: APPROVED
2. Request OFF → Expected: APPROVED
3. Request ON for 10 seconds (exceeds limit) → Expected: REJECTED
4. Request OFF → Expected: APPROVED

**Expected Log Output**:
```
[I][dummy.actuator:XXX]: TEST 2: MAXIMUM DURATION ENFORCEMENT
[I][actuator.safety:XXX]: APPROVED: TestActuator ON (max duration: 3 seconds)
[I][actuator.safety:XXX]: APPROVED: TestActuator OFF
[W][actuator.safety:XXX]: REJECTED: TestActuator - Max duration violation - requested 10s exceeds limit 5s
```

### Test 3: Normal Operation

**Purpose**: Validate that normal operations work correctly

**Sequence**:
1. Request ON for 2 seconds → Expected: APPROVED
2. Request OFF → Expected: APPROVED
3. Request ON for 1 second → Expected: APPROVED
4. Request OFF → Expected: APPROVED
5. Display actuator statistics

**Expected Log Output**:
```
[I][dummy.actuator:XXX]: TEST 3: NORMAL OPERATION
[I][actuator.safety:XXX]: APPROVED: TestActuator ON (max duration: 2 seconds)
[I][actuator.safety:XXX]: APPROVED: TestActuator OFF
[I][dummy.actuator:XXX]: Final Stats:
[I][dummy.actuator:XXX]:   State: OFF
[I][dummy.actuator:XXX]:   Runtime: 0 seconds
[I][dummy.actuator:XXX]:   Max Duration: 5 seconds
```

## Hardware Setup

The component controls an LED connected to a GPIO pin for visual feedback:

**Wiring**:
```
ESP32-C6 GPIO 4 ──[LED]──[Resistor 220Ω]──[GND]

LED Behavior:
- ON:  Safety gate approved the command
- OFF: Safety gate rejected or actuator turned off
```

**Note**: GPIO 4 is used because GPIO 8 is reserved for the built-in WS2812 RGB LED.

## Configuration

### YAML Configuration

```yaml
# Required: Safety gate instance
actuator_safety_gate:
  id: actuator_safety

# Test trigger component
dummy_actuator_trigger:
  id: test_trigger
  safety_gate: actuator_safety  # Reference to safety gate
  led_pin: GPIO4                # GPIO pin for test LED
  test_interval: 15s            # How often to run test sequences
```

### Configuration Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `safety_gate` | ID | Yes | - | Reference to ActuatorSafetyGate component |
| `led_pin` | GPIO | No | - | GPIO pin for visual feedback LED |
| `test_interval` | Time | No | 10s | Interval between test sequences |

## Usage

### Monitoring Test Results

Watch the serial monitor to see test results:

```bash
# Using ESPHome
esphome logs plantOS.yaml

# Using Task runner
task snoop
```

### Expected Test Cycle

With default 15-second interval:
- **0s**: System boots, components initialize
- **15s**: Test 1 (Debouncing) runs
- **30s**: Test 2 (Duration Limits) runs
- **45s**: Test 3 (Normal Operation) runs
- **60s**: Test 1 runs again (cycle repeats)

### LED Behavior During Tests

- **Test 1**: LED blinks ON-OFF multiple times (some rejected)
- **Test 2**: LED turns ON briefly, then stays OFF (duration violation rejected)
- **Test 3**: LED blinks ON-OFF normally (all approved)

## Development Notes

### Adding New Tests

To add a new test sequence:

1. Add a new test method in `dummy_actuator_trigger.cpp`:
```cpp
void DummyActuatorTrigger::test_my_new_feature() {
    ESP_LOGI(TAG, "TEST 4: MY NEW FEATURE");
    // Your test logic here
}
```

2. Update `run_test_sequence()` to include the new test:
```cpp
case 3:
    test_my_new_feature();
    break;
```

3. Update the modulo in `run_test_sequence()`:
```cpp
current_test_ = (current_test_ + 1) % 4;  // Changed from 3 to 4
```

### Customizing Test Parameters

The test actuator is configured in `setup()`:
```cpp
safety_gate_->setMaxDuration("TestActuator", 5);  // Change max duration here
```

Modify this value to test different duration limits.

## Troubleshooting

### Tests Not Running

**Symptom**: No test sequences appear in logs

**Causes**:
1. Safety gate not configured: Check that `safety_gate` ID is correct
2. Component not initialized: Check logs for initialization message
3. Test interval too long: Check `test_interval` configuration

**Solution**:
```bash
# Check component initialization
esphome logs plantOS.yaml | grep "dummy.actuator"

# Should see:
# [I][dummy.actuator:XXX]: DummyActuatorTrigger initialized
```

### LED Not Lighting

**Symptom**: LED doesn't respond to test sequences

**Causes**:
1. Wrong GPIO pin configured
2. LED wired incorrectly
3. LED pin not configured in YAML

**Solution**:
1. Check wiring: GPIO 4 → LED anode (+), LED cathode (-) → 220Ω resistor → GND
2. Verify YAML configuration includes `led_pin: GPIO4`
3. Check logs for "LED configured on GPIO X" message

### All Commands Rejected

**Symptom**: Every test command shows REJECTED

**Causes**:
1. Max duration set too low
2. Safety gate not properly initialized

**Solution**:
Check safety gate logs for specific rejection reasons

## Integration with Real Systems

This test component can be used as a template for real actuator control:

```cpp
// Based on dummy_actuator_trigger.cpp
void MyActuatorController::doseAcid(int seconds) {
    if (safety_gate_->executeCommand("AcidPump", true, seconds)) {
        // Approved - turn on pump
        digitalWrite(ACID_PUMP_PIN, HIGH);

        // Schedule auto-shutoff
        set_timeout("acid_shutoff", seconds * 1000, [this]() {
            safety_gate_->executeCommand("AcidPump", false);
            digitalWrite(ACID_PUMP_PIN, LOW);
        });
    } else {
        ESP_LOGE(TAG, "Acid pump command rejected");
    }
}
```

## License

Part of the PlantOS project. See main project LICENSE.
