#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/actuator_safety_gate/ActuatorSafetyGate.h"

namespace esphome {
namespace dummy_actuator_trigger {

/**
 * DummyActuatorTrigger - Test Component for ActuatorSafetyGate
 *
 * This component runs automated test sequences to validate the ActuatorSafetyGate's
 * safety features including debouncing and maximum duration enforcement.
 *
 * Test Sequences:
 * 1. Debounce Test: Tries to turn ON the actuator multiple times rapidly
 * 2. Duration Limit Test: Requests durations that exceed configured limits
 * 3. Normal Operation Test: Valid ON/OFF cycles
 *
 * The component controls a physical LED on a GPIO pin to provide visual feedback.
 */
class DummyActuatorTrigger : public Component {
public:
    DummyActuatorTrigger();

    void setup() override;
    void loop() override;

    /**
     * Set the safety gate instance to use for testing
     * @param gate Pointer to ActuatorSafetyGate instance
     */
    void set_safety_gate(actuator_safety_gate::ActuatorSafetyGate *gate) {
        safety_gate_ = gate;
    }

    /**
     * Set the GPIO pin for the test LED
     * @param pin GPIOPin pointer
     */
    void set_led_pin(GPIOPin *pin) {
        led_pin_ = pin;
    }

    /**
     * Set the test interval (how often to run test sequences)
     * @param interval_ms Interval in milliseconds
     */
    void set_test_interval(uint32_t interval_ms) {
        test_interval_ = interval_ms;
    }

private:
    actuator_safety_gate::ActuatorSafetyGate *safety_gate_{nullptr};
    GPIOPin *led_pin_{nullptr};  // GPIO pin for LED
    uint32_t test_interval_{10000};  // Default: 10 seconds
    uint32_t last_test_time_{0};
    uint8_t current_test_{0};  // Which test sequence we're on
    bool actuator_state_{false};  // Physical LED state

    // Test sequence methods
    void run_test_sequence();
    void test_debouncing();
    void test_duration_limits();
    void test_normal_operation();

    // Helper to control the physical LED based on safety gate approval
    void control_actuator(const char* actuator_id, bool target_state, int duration = 0);
};

} // namespace dummy_actuator_trigger
} // namespace esphome
