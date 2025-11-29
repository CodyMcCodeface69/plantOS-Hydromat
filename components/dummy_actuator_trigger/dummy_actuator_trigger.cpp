#include "dummy_actuator_trigger.h"

namespace esphome {
namespace dummy_actuator_trigger {

static const char *TAG = "dummy.actuator";

DummyActuatorTrigger::DummyActuatorTrigger() {
}

void DummyActuatorTrigger::setup() {
    ESP_LOGI(TAG, "DummyActuatorTrigger initialized");

    // Configure LED pin if specified
    if (led_pin_ != nullptr) {
        led_pin_->setup();
        led_pin_->digital_write(false);
        ESP_LOGI(TAG, "LED configured on pin");
    } else {
        ESP_LOGW(TAG, "No LED pin configured - tests will run without visual feedback");
    }

    // Validate safety gate is configured
    if (safety_gate_ == nullptr) {
        ESP_LOGE(TAG, "ERROR: Safety gate not configured! Tests will not run.");
        return;
    }

    // Configure the safety gate for our test actuator
    safety_gate_->setMaxDuration("TestActuator", 5);  // 5 seconds max
    ESP_LOGI(TAG, "Test actuator configured with 5-second max duration");

    // Initialize test state
    last_test_time_ = millis();
    current_test_ = 0;

    ESP_LOGI(TAG, "Test sequences will run every %u seconds", test_interval_ / 1000);
    ESP_LOGI(TAG, "Tests: 1=Debouncing, 2=Duration Limits, 3=Normal Operation");
}

void DummyActuatorTrigger::loop() {
    // Only run tests if safety gate is configured
    if (safety_gate_ == nullptr) return;

    // Check if it's time to run the next test
    uint32_t current_time = millis();
    if (current_time - last_test_time_ >= test_interval_) {
        run_test_sequence();
        last_test_time_ = current_time;
    }
}

void DummyActuatorTrigger::run_test_sequence() {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  TEST SEQUENCE #%d", current_test_ + 1);
    ESP_LOGI(TAG, "========================================");

    switch (current_test_) {
        case 0:
            test_debouncing();
            break;
        case 1:
            test_duration_limits();
            break;
        case 2:
            test_normal_operation();
            break;
    }

    // Cycle through tests
    current_test_ = (current_test_ + 1) % 3;

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
}

void DummyActuatorTrigger::test_debouncing() {
    ESP_LOGI(TAG, "TEST 1: DEBOUNCING PROTECTION");
    ESP_LOGI(TAG, "Expected: 1st ON approved, 2nd ON rejected");
    ESP_LOGI(TAG, "");

    // First ON command - should be approved
    ESP_LOGI(TAG, "[1] Requesting ON...");
    control_actuator("TestActuator", true, 3);
    delay(100);

    // Second ON command - should be REJECTED (debouncing)
    ESP_LOGI(TAG, "[2] Requesting ON again (testing debounce)...");
    control_actuator("TestActuator", true, 3);
    delay(100);

    // Turn OFF to reset
    ESP_LOGI(TAG, "[3] Requesting OFF...");
    control_actuator("TestActuator", false);
    delay(100);

    // Try ON again after OFF - should be approved
    ESP_LOGI(TAG, "[4] Requesting ON after OFF (should work)...");
    control_actuator("TestActuator", true, 2);
    delay(100);

    // Clean up - turn OFF
    delay(500);
    control_actuator("TestActuator", false);
}

void DummyActuatorTrigger::test_duration_limits() {
    ESP_LOGI(TAG, "TEST 2: MAXIMUM DURATION ENFORCEMENT");
    ESP_LOGI(TAG, "Expected: 3s request approved, 10s request rejected");
    ESP_LOGI(TAG, "Max configured duration: 5 seconds");
    ESP_LOGI(TAG, "");

    // Request within limit (3 seconds) - should be approved
    ESP_LOGI(TAG, "[1] Requesting ON for 3 seconds (within limit)...");
    control_actuator("TestActuator", true, 3);
    delay(100);

    // Turn OFF
    ESP_LOGI(TAG, "[2] Requesting OFF...");
    control_actuator("TestActuator", false);
    delay(100);

    // Request exceeding limit (10 seconds) - should be REJECTED
    ESP_LOGI(TAG, "[3] Requesting ON for 10 seconds (exceeds 5s limit)...");
    control_actuator("TestActuator", true, 10);
    delay(100);

    // Turn OFF (if somehow it was turned on)
    ESP_LOGI(TAG, "[4] Requesting OFF...");
    control_actuator("TestActuator", false);
}

void DummyActuatorTrigger::test_normal_operation() {
    ESP_LOGI(TAG, "TEST 3: NORMAL OPERATION");
    ESP_LOGI(TAG, "Expected: All commands approved");
    ESP_LOGI(TAG, "");

    // Normal ON cycle
    ESP_LOGI(TAG, "[1] Requesting ON for 2 seconds...");
    control_actuator("TestActuator", true, 2);
    delay(500);

    // Turn OFF
    ESP_LOGI(TAG, "[2] Requesting OFF...");
    control_actuator("TestActuator", false);
    delay(500);

    // Another ON cycle
    ESP_LOGI(TAG, "[3] Requesting ON for 1 second...");
    control_actuator("TestActuator", true, 1);
    delay(500);

    // Final OFF
    ESP_LOGI(TAG, "[4] Requesting OFF...");
    control_actuator("TestActuator", false);
    delay(100);

    // Check runtime stats
    bool state;
    uint32_t runtime, maxDuration;
    if (safety_gate_->getStats("TestActuator", state, runtime, maxDuration)) {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "Final Stats:");
        ESP_LOGI(TAG, "  State: %s", state ? "ON" : "OFF");
        ESP_LOGI(TAG, "  Runtime: %u seconds", runtime);
        ESP_LOGI(TAG, "  Max Duration: %u seconds", maxDuration);
    }
}

void DummyActuatorTrigger::control_actuator(const char* actuator_id,
                                            bool target_state,
                                            int duration) {
    // Execute command through safety gate
    bool approved = safety_gate_->executeCommand(actuator_id, target_state, duration);

    if (approved) {
        // Command approved - update physical LED
        if (led_pin_ != nullptr) {
            led_pin_->digital_write(target_state);
            actuator_state_ = target_state;
        }
        ESP_LOGI(TAG, "  -> LED %s", target_state ? "ON" : "OFF");
    } else {
        ESP_LOGW(TAG, "  -> Command REJECTED by safety gate");
    }
}

} // namespace dummy_actuator_trigger
} // namespace esphome
