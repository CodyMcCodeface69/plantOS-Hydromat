#include "controller.h"
#include "esphome/core/log.h"

namespace esphome {
namespace controller {

static const char *TAG = "controller.fsm";

// ============================================================================
// STATE: ERROR - Fault Condition Alert
// ============================================================================
/**
 * Fast red flashing for 5 seconds, then restart from INIT.
 *
 * PURPOSE:
 * - Visual alert for fault conditions (simulated via random trigger)
 * - In production, this would be triggered by:
 *   * Sensor reading out of range
 *   * Communication timeout
 *   * Calibration failure
 *   * Low battery or power issues
 * - Distinctive pattern (fast flashing) is attention-grabbing
 *
 * VISUAL: Fast Red Flashing
 * - Color: Pure red (r=1.0, g=0.0, b=0.0)
 * - Pattern: 100ms ON / 100ms OFF = 5 Hz
 * - Duration: 5 seconds total (25 flashes)
 *
 * WHY FAST FLASHING (5 Hz vs slower rates):
 * - URGENCY: Fast flashing universally signals "error" or "danger"
 * - ATTENTION: More likely to be noticed in peripheral vision
 * - DISTINCTION: Clearly different from calibration blink (1 Hz)
 * - Not too fast: 5 Hz is fast but not seizure-inducing (>10 Hz is risky)
 *
 * WHY 5 SECONDS:
 * - Long enough to be noticed if user isn't watching
 * - Long enough for user to investigate and check logs
 * - Short enough to not be eternally annoying
 * - Provides time for potential manual intervention before restart
 *
 * WHY RETURN TO INIT (not CALIBRATION or READY):
 * - Full restart provides cleanest recovery path
 * - Allows sensors to re-calibrate after fault
 * - User sees boot sequence again (confirmation of restart)
 * - In production, this could trigger deeper diagnostics
 */
Controller::StateFunc Controller::state_error() {
  uint32_t elapsed = millis() - this->state_start_time_;

  // After 5 seconds in ERROR state, perform full restart
  if (elapsed > 5000) {
    ESP_LOGI(TAG, "Error cleared. Re-initializing...");
    return &Controller::state_init; // Full restart from INIT
  }

  /**
   * Fast flashing pattern at 5 Hz.
   *
   * FLASHING MATH:
   * - elapsed / 100: Number of 100ms intervals passed
   * - % 2: Alternates between 0 (even) and 1 (odd)
   * - even = ON, odd = OFF
   * - Results in 5 Hz flashing (10 state changes per second)
   */
  bool on = (elapsed / 100) % 2 == 0;
  if (on) apply_light(1.0, 0.0, 0.0, 1.0);  // Red at full brightness
  else    apply_light(0.0, 0.0, 0.0, 0.0);  // Off

  // Stay in ERROR state until timeout
  return &Controller::state_error;
}

} // namespace controller
} // namespace esphome
