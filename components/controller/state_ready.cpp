#include "controller.h"
#include "esphome/core/log.h"
#include <cmath>

namespace esphome {
namespace controller {

static const char *TAG = "controller.fsm";

// ============================================================================
// STATE: READY - Normal Operation
// ============================================================================
/**
 * Normal operating state with breathing green animation and sensor monitoring.
 *
 * PURPOSE:
 * - Indicates system is functioning normally and monitoring sensors
 * - Breathing effect shows system is "alive" (not frozen)
 * - Sensor threshold monitoring triggers ERROR state when exceeded
 *
 * VISUAL: Breathing Green Animation
 * - Base color: Pure green (r=0.0, g=1.0, b=0.0)
 * - Brightness: Sine wave modulation from 10% to 100%
 * - Period: ~1 second per breath cycle
 * - Effect: "Inhale/exhale" pulsing
 *
 * SENSOR MONITORING:
 * - Checks sensor value against threshold once per second
 * - High threshold (>90): Triggers ERROR state
 * - Provides real sensor-based alerts instead of random errors
 *
 * TWO-PHASE IMPLEMENTATION:
 * 1. Transition logic (checked once per second)
 * 2. Visual logic (updated every loop iteration for smooth animation)
 */
Controller::StateFunc Controller::state_ready() {
  uint32_t elapsed = millis() - this->state_start_time_;
  uint32_t current_seconds = elapsed / 1000;

  // =========================================================================
  // PHASE 1: Stochastic Transition Logic
  // =========================================================================
  /**
   * Check sensor threshold ONCE PER SECOND.
   *
   * WHY ONCE PER SECOND (not every loop iteration):
   * - loop() runs ~1000 times per second
   * - Without gating, we'd trigger errors 1000 times per second
   * - state_counter_ tracks which second we're in, only checks on new seconds
   * - Prevents error state spam when threshold is consistently violated
   *
   * HOW state_counter_ WORKS:
   * - current_seconds = elapsed / 1000 converts ms to seconds
   * - When current_seconds > state_counter_, a new second has started
   * - We update state_counter_ and perform threshold check
   * - Subsequent loop iterations in the same second skip this block
   *
   * THRESHOLD STRATEGY:
   * - High threshold (>90): Triggers alert for excessive sensor readings
   * - Could indicate over-watering, sensor malfunction, or critical condition
   * - For plant monitoring: adjust threshold based on plant species needs
   */
  if (current_seconds > this->state_counter_) {
    this->state_counter_ = current_seconds;

    // Check high threshold: trigger error if value exceeds 90
    if (this->current_sensor_value_ > 90.0f) {
       ESP_LOGW(TAG, "High threshold exceeded! Sensor value: %.1f", this->current_sensor_value_);
       return &Controller::state_error;
    }
  }

  // =========================================================================
  // PHASE 2: Visual Logic - Breathing Animation
  // =========================================================================
  /**
   * Sine wave brightness modulation for "breathing" effect.
   *
   * MATH BREAKDOWN:
   * - t = millis() / 1000.0f: Convert ms to seconds (with decimals)
   * - sin(t * π): Sine wave with ~1 second period (2π radians)
   * - Range of sin(): -1.0 to +1.0
   * - (sin() + 1.0) / 2.0: Shift and scale to 0.0 to 1.0
   * - 0.1 + (brightness * 0.9): Remap to 10% to 100%
   *
   * WHY NOT 0% MINIMUM:
   * - 0% brightness = LED completely off
   * - Could be mistaken for system hang or power loss
   * - 10% minimum ensures LED is always visible
   * - Still creates strong breathing effect (10x brightness variation)
   *
   * WHY SINE WAVE (vs linear ramp or triangle wave):
   * - Sine accelerates smoothly (no sharp transitions)
   * - Natural-looking "breathing" motion
   * - Matches biological breathing rhythm (smooth acceleration/deceleration)
   *
   * WHY π (3.14159):
   * - Period of sin(t * π) is 2 seconds (full cycle: 0→π→2π)
   * - Creates slow, calming breathing effect
   * - Using π*2 would make it twice as fast (more agitated feeling)
   *
   * PERFORMANCE NOTE:
   * - sin() is computed every loop iteration (~1000 Hz)
   * - ESP32 has hardware floating-point, so this is fast enough
   * - Alternative: Lookup table would be faster but uses more memory
   */
  float t = millis() / 1000.0f;
  float brightness = (std::sin(t * 3.14159f) + 1.0f) / 2.0f; // 0.0 to 1.0
  brightness = 0.1f + (brightness * 0.9f); // Remap to 10% to 100%

  apply_light(0.0, 1.0, 0.0, brightness);

  // Stay in READY state (unless random error triggered above)
  return &Controller::state_ready;
}

} // namespace controller
} // namespace esphome
