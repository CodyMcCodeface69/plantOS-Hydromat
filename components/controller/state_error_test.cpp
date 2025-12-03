#include "controller.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cmath>

namespace esphome {
namespace controller {

static const char *TAG = "controller.state.error_test";

/**
 * ERROR_TEST State Implementation
 *
 * This is a special test state used by PSMChecker to validate persistent
 * state recovery. It differs from the normal ERROR state in that:
 * - It stays active indefinitely (doesn't auto-reset)
 * - Uses blue/purple LED pattern for visual distinction
 * - Indicates the system is in PSM test mode
 *
 * Visual Feedback:
 * - Pulsing blue/purple LED (breathing effect)
 * - Cycle period: 2 seconds (slower than normal ERROR)
 * - Color: Blue (0, 0, 1) with brightness modulation
 *
 * State Transitions:
 * - NEXT STATE: Stays in ERROR_TEST (requires manual reset or power cycle)
 * - EXIT: Only via reset_to_init() or power cycle
 */
Controller::StateFunc Controller::state_error_test() {
    uint32_t elapsed = millis() - this->state_start_time_;

    /**
     * Log entry into ERROR_TEST state (once)
     *
     * We use state_counter_ to track if we've logged the entry message.
     * This prevents spamming the log every loop iteration.
     */
    if (this->state_counter_ == 0) {
        ESP_LOGI(TAG, "Entered ERROR_TEST state");
        ESP_LOGI(TAG, "Showing pulsing blue/purple LED pattern");
        ESP_LOGI(TAG, "This state will persist until reset or power cycle");
        if (this->verbose_) {
            ESP_LOGD(TAG, "[VERBOSE] ERROR_TEST: State entered, breathing pattern active");
        }
        this->state_counter_ = 1;  // Mark as logged
    }

    /**
     * Calculate breathing effect for blue LED
     *
     * Uses sine wave with 2-second period for smooth pulsing.
     * The breathing effect makes it clear the system is still running,
     * not frozen or crashed.
     *
     * Math breakdown:
     * - elapsed_seconds: Time in state as float
     * - sin_input: Converts time to sine wave input (2π per 2 seconds)
     * - sin_value: Sine wave oscillating between -1 and +1
     * - brightness: Scaled to 0.2-1.0 (never fully off, easier to see)
     */
    float elapsed_seconds = elapsed / 1000.0f;
    float sin_input = elapsed_seconds * M_PI;  // π per second = 2s period
    float sin_value = std::sin(sin_input);
    float brightness = 0.5f + 0.5f * sin_value;  // Range: 0.0 to 1.0

    // Log breathing animation once per second in verbose mode
    static uint32_t last_breathing_log_second = 0;
    uint32_t current_second = elapsed / 1000;
    if (this->verbose_ && current_second > last_breathing_log_second) {
        ESP_LOGD(TAG, "[VERBOSE] ERROR_TEST: Breathing animation (brightness: %.2f, elapsed: %lus)",
                 brightness, current_second);
        last_breathing_log_second = current_second;
    }

    // Apply blue/purple LED with breathing brightness
    // Blue (0, 0, 1) with slight purple tint (0.2, 0, 1)
    log_action_start("Update ERROR_TEST breathing LED");
    apply_light(0.2f, 0.0f, 1.0f, brightness);
    log_action_end("Update ERROR_TEST breathing LED");

    /**
     * State Persistence
     *
     * Unlike other states that transition automatically, ERROR_TEST
     * stays active indefinitely. This allows:
     * 1. User to observe the LED pattern
     * 2. User to unplug device while in this state
     * 3. PSM to recover this state on next boot
     *
     * The state can only be exited by:
     * - Calling reset_to_init()
     * - Power cycling the device
     * - Reflashing firmware
     */
    return &Controller::state_error_test;  // Stay in this state
}

} // namespace controller
} // namespace esphome
