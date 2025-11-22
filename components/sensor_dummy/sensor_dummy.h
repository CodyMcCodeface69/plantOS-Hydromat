#pragma once
#include "esphome.h"

namespace esphome {
namespace sensor_dummy {

/**
 * SensorDummy: A simple polling sensor for testing and development
 *
 * ARCHITECTURE:
 * This component demonstrates ESPHome's PollingComponent pattern, which is
 * appropriate for sensors that need periodic updates rather than interrupt-
 * driven or callback-based updates.
 *
 * LIFECYCLE:
 * 1. setup()  - Called once during ESP32 boot
 * 2. update() - Called repeatedly at 'update_interval' (configurable in YAML)
 *
 * WHY MULTIPLE INHERITANCE:
 * - sensor::Sensor provides:
 *   - State storage (this->state)
 *   - publish_state() for pushing updates to subscribers
 *   - Callback registration (add_on_state_callback)
 *   - Filtering (moving average, throttle, etc.)
 *   - Home Assistant integration
 *
 * - PollingComponent provides:
 *   - update() method called at regular intervals
 *   - update_interval configuration from YAML
 *   - Automatic timing management (no manual millis() tracking needed)
 *
 * WHEN TO USE THIS PATTERN:
 * - Sensors that need periodic sampling (temperature, humidity, soil moisture)
 * - When measurement timing isn't critical (±100ms variance is acceptable)
 * - When you want ESPHome to manage timing instead of manual millis() checks
 *
 * WHEN NOT TO USE:
 * - Interrupt-driven sensors (button presses, rotary encoders)
 * - High-frequency sampling where precise timing matters
 * - Event-driven sensors (motion detectors, door sensors)
 */
class SensorDummy : public sensor::Sensor, public PollingComponent {
public:
  /**
   * update() is called automatically by PollingComponent at the interval
   * specified in YAML (default: 1 second for this component).
   *
   * This is where sensor reading and processing happens.
   */
  void update() override;

  /**
   * setup() is called once during ESP32 initialization.
   *
   * WHY PUBLISH INITIAL STATE:
   * Publishing an initial value (0) ensures that any components subscribing
   * to this sensor (like our controller) receive a valid value immediately
   * rather than waiting for the first update() call. This prevents undefined
   * behavior in controllers that might make decisions based on sensor data.
   */
  void setup() override;
};

} // namespace sensor_dummy
} // namespace esphome
