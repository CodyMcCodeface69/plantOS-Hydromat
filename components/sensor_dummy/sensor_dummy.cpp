#include "sensor_dummy.h"

namespace esphome {
namespace sensor_dummy {

void SensorDummy::setup() {
  /**
   * Initialize the sensor with a known starting value (0).
   *
   * WHY PUBLISH IN SETUP:
   * - Ensures subscribers (e.g., controller) have valid data immediately
   * - Prevents race conditions where controller might read undefined state
   * - Establishes a predictable starting point for testing/debugging
   *
   * ALTERNATIVE APPROACHES NOT USED:
   * - Could wait for first update() call: But this leaves a brief window
   *   where state is undefined (NaN). Components checking sensor state
   *   during their setup() would get invalid data.
   * - Could initialize state directly: But publish_state() triggers callbacks
   *   and ensures subscribers are notified properly.
   */
  publish_state(0);
}

void SensorDummy::update() {
  /**
   * Generate a simple cycling pattern: 0 → 10 → 20 → ... → 100 → 0
   *
   * WHY THIS IMPLEMENTATION:
   * - Predictable: Always the same sequence, easy to verify in logs
   * - Simple: Minimal logic, easy to understand and debug
   * - Observable: 10-unit increments are large enough to see in UI/logs
   * - Bounded: Wraps at 100 to prevent unbounded growth
   *
   * DESIGN TRADE-OFFS:
   * - Could use random values: But would make controller behavior
   *   non-deterministic and harder to test
   * - Could use sine wave: But would require floating-point math and
   *   wouldn't test integer value handling
   * - Could use millis() for smooth values: But update() already runs
   *   periodically, so incrementing per call is simpler
   *
   * REAL SENSOR IMPLEMENTATION:
   * In production, this would be replaced with actual sensor reading:
   *   float moisture = analogRead(SENSOR_PIN) / 4095.0 * 100.0;
   *   publish_state(moisture);
   */

  // Read current published state (inherited from sensor::Sensor base class)
  float current = this->state;

  // Increment by 10, wrap to 0 if exceeding 100
  float next = current + 10.0;
  if (next > 100.0) {
    next = 0.0;
  }

  // Log at DEBUG level so it can be filtered out in production
  ESP_LOGD("sensor_dummy", "Updating dummy sensor value to: %f", next);

  /**
   * publish_state() is the ESPHome pattern for sensor updates.
   *
   * What it does:
   * - Updates this->state (base class member)
   * - Triggers all registered callbacks (e.g., controller's sensor listener)
   * - Applies any configured filters (moving average, throttle, etc.)
   * - Sends data to Home Assistant (if connected)
   * - Logs the value change
   *
   * WHY NOT JUST SET this->state DIRECTLY:
   * Direct assignment would bypass the callback mechanism, breaking
   * component interconnections. The controller would never receive updates.
   */
  publish_state(next);
}

} // namespace sensor_dummy
} // namespace esphome
