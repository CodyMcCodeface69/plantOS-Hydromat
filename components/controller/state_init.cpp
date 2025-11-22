#include "controller.h"
#include "esphome/core/log.h"

namespace esphome {
namespace controller {

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

  // Time-based color selection using simple cascading if-else
  // WHY NOT switch/case: elapsed is continuous, not discrete values
  if (elapsed < 1000) {
    apply_light(1.0, 0.0, 0.0); // Red (full intensity)
  } else if (elapsed < 2000) {
    apply_light(1.0, 1.0, 0.0); // Yellow (red + green = yellow)
  } else if (elapsed < 3000) {
    apply_light(0.0, 1.0, 0.0); // Green (full intensity)
  } else {
    // After 3 seconds, proceed to calibration
    return &Controller::state_calibration;
  }

  // Stay in INIT state (called every loop iteration, ~1000 Hz)
  return &Controller::state_init;
}

} // namespace controller
} // namespace esphome
