#include "controller.h"
#include "esphome/core/log.h"

namespace esphome {
namespace controller {

static const char *TAG = "controller.fsm";

// ============================================================================
// STATE: CALIBRATION - Sensor Stabilization
// ============================================================================
/**
 * Blinking yellow pattern for 4 seconds, then transition to READY.
 *
 * PURPOSE:
 * - Simulates sensor calibration/stabilization period
 * - In production, this state could perform actual sensor warmup:
 *   * Soil moisture sensors need time to stabilize readings
 *   * Temperature sensors may need thermal equilibration
 *   * Air quality sensors often require 30-60s warmup
 * - Visual feedback that system is preparing, not ready yet
 *
 * BLINKING PATTERN:
 * - 500ms ON / 500ms OFF = 1 Hz blink rate
 * - Color: Amber (r=1.0, g=0.8, b=0.0) - warmer than pure yellow
 * - Duration: 4 seconds (8 blinks total)
 *
 * WHY 500ms INTERVALS:
 * - 1 Hz is standard for "busy" or "processing" indicators
 * - Fast enough to show activity, slow enough to not induce seizures
 * - Matches user expectations from other devices
 *
 * WHY 4 SECONDS TOTAL:
 * - Long enough to be noticeable (not skipped too fast)
 * - Short enough to not annoy users
 * - In production, actual calibration time would be sensor-dependent
 *
 * BLINKING MATH:
 * elapsed / 500 gives number of 500ms intervals passed
 * % 2 alternates between 0 (even) and 1 (odd)
 * even intervals = ON, odd intervals = OFF
 */
Controller::StateFunc Controller::state_calibration() {
  uint32_t elapsed = millis() - this->state_start_time_;

  // Track blink state for verbose logging (only log on state changes)
  static bool last_blink_state = false;

  // Blink at 1 Hz (500ms period)
  bool on = (elapsed / 500) % 2 == 0;

  if (this->verbose_ && on != last_blink_state) {
    ESP_LOGD(TAG, "[VERBOSE] CALIBRATION: LED blink %s (elapsed: %lums)",
             on ? "ON" : "OFF", elapsed);
    last_blink_state = on;
  }

  log_action_start(on ? "Set LED to AMBER" : "Turn LED OFF");
  if (on) apply_light(1.0, 0.8, 0.0, 1.0);  // Amber (warm yellow)
  else    apply_light(0.0, 0.0, 0.0, 0.0);  // Off (brightness = 0)
  log_action_end(on ? "Set LED to AMBER" : "Turn LED OFF");

  // After 4 seconds, transition to READY state
  if (elapsed > 4000) {
    if (this->verbose_) {
      ESP_LOGD(TAG, "[VERBOSE] CALIBRATION: Complete, transitioning to READY");
    }
    last_blink_state = false; // Reset for next calibration
    return &Controller::state_ready;
  }

  // Stay in CALIBRATION state
  return &Controller::state_calibration;
}

} // namespace controller
} // namespace esphome
