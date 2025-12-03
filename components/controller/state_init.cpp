#include "controller.h"
#include "esphome/core/log.h"

namespace esphome {
namespace controller {

static const char *TAG = "controller.fsm";

// ============================================================================
// STATE: INIT - Boot Sequence
// ============================================================================
/**
 * Visual boot sequence: Red (1s) → Yellow (1s) → Green (1s) → CALIBRATION
 *
 * PURPOSE:
 * - Visual confirmation that the device is booting
 * - Allows user to verify LED is working during startup
 * - Traditional traffic light pattern is universally recognizable
 *
 * TIMING BREAKDOWN:
 * - 0-1000ms:   Red    (danger/stop - system initializing)
 * - 1000-2000ms: Yellow (caution - system starting)
 * - 2000-3000ms: Green  (ready - system about to enter calibration)
 * - 3000ms+:    Transition to CALIBRATION
 *
 * WHY THESE COLORS:
 * - Red:    Universally associated with "starting up" or "not ready"
 * - Yellow: Transitional state, "getting ready"
 * - Green:  "Go" signal, transitioning to active operation
 *
 * WHY 1 SECOND INTERVALS:
 * - Fast enough to keep boot time reasonable (3s total)
 * - Slow enough for humans to clearly see each color
 * - Matches user expectations from other devices (routers, etc.)
 */
Controller::StateFunc Controller::state_init() {
  uint32_t elapsed = millis() - this->state_start_time_;

  // Track which second we're in for verbose logging (only log once per phase)
  static uint32_t last_logged_phase = 0;
  uint32_t current_phase = elapsed / 1000; // 0, 1, 2, or 3+

  // Time-based color selection using simple cascading if-else
  // WHY NOT switch/case: elapsed is continuous, not discrete values
  if (elapsed < 1000) {
    if (this->verbose_ && current_phase != last_logged_phase) {
      ESP_LOGD(TAG, "[VERBOSE] INIT: Phase 1/3 - RED (0-1s)");
      last_logged_phase = current_phase;
    }
    log_action_start("Set LED to RED");
    apply_light(1.0, 0.0, 0.0); // Red (full intensity)
    log_action_end("Set LED to RED");
  } else if (elapsed < 2000) {
    if (this->verbose_ && current_phase != last_logged_phase) {
      ESP_LOGD(TAG, "[VERBOSE] INIT: Phase 2/3 - YELLOW (1-2s)");
      last_logged_phase = current_phase;
    }
    log_action_start("Set LED to YELLOW");
    apply_light(1.0, 1.0, 0.0); // Yellow (red + green = yellow)
    log_action_end("Set LED to YELLOW");
  } else if (elapsed < 3000) {
    if (this->verbose_ && current_phase != last_logged_phase) {
      ESP_LOGD(TAG, "[VERBOSE] INIT: Phase 3/3 - GREEN (2-3s)");
      last_logged_phase = current_phase;
    }
    log_action_start("Set LED to GREEN");
    apply_light(0.0, 1.0, 0.0); // Green (full intensity)
    log_action_end("Set LED to GREEN");
  } else {
    // After 3 seconds, proceed to calibration
    if (this->verbose_) {
      ESP_LOGD(TAG, "[VERBOSE] INIT: Boot sequence complete, transitioning to CALIBRATION");
    }
    last_logged_phase = 0; // Reset for next time we enter INIT
    return &Controller::state_calibration;
  }

  // Stay in INIT state (called every loop iteration, ~1000 Hz)
  return &Controller::state_init;
}

} // namespace controller
} // namespace esphome
