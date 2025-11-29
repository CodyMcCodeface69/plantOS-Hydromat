#pragma once

#include "esphome.h"
#include "RobustAverager.h"

namespace esphome {
namespace sensor_filter {

/**
 * SensorFilter: Robust averaging filter with outlier rejection
 *
 * ARCHITECTURE:
 * This component acts as a filtering layer between a source sensor and consumers.
 * It subscribes to a source sensor's updates, collects readings in a buffer,
 * applies robust averaging with outlier rejection, and publishes filtered values.
 *
 * WORKFLOW:
 * 1. Source sensor updates → on_sensor_update() callback triggered
 * 2. Reading added to RobustAverager buffer
 * 3. When buffer is full → calculate robust average (with outlier rejection)
 * 4. Publish filtered value → consumers receive clean data
 * 5. Buffer auto-resets → start collecting next window
 *
 * LIFECYCLE:
 * 1. setup()       - Subscribe to source sensor callbacks
 * 2. loop()        - Not used (event-driven, not polling)
 * 3. on_sensor_update() - Called when source sensor publishes new value
 *
 * WHY Component (not PollingComponent):
 * This filter is reactive/event-driven - it processes data when the source
 * sensor updates, rather than polling at fixed intervals. The source sensor
 * controls the update timing.
 *
 * USE CASES:
 * - pH sensors: Reject calibration drift outliers
 * - Temperature sensors: Filter EMI spikes and noise
 * - Moisture sensors: Smooth out rapid fluctuations
 * - Any sensor prone to occasional bad readings
 *
 * EXAMPLE CONFIGURATION:
 * ```yaml
 * sensor:
 *   - platform: sensor_dummy
 *     id: raw_sensor
 *     name: "Raw Sensor"
 *     update_interval: 500ms
 *
 *   - platform: sensor_filter
 *     id: filtered_sensor
 *     name: "Filtered Sensor"
 *     sensor_source: raw_sensor
 *     window_size: 20
 *     reject_percentage: 0.10
 * ```
 */
class SensorFilter : public sensor::Sensor, public Component {
public:
  /**
   * Set the source sensor to filter
   *
   * @param sensor Pointer to the source sensor
   */
  void set_sensor_source(sensor::Sensor *sensor) {
    sensor_source_ = sensor;
  }

  /**
   * Set the window size (number of readings to collect)
   *
   * @param size Window size (2-100)
   */
  void set_window_size(int size) {
    window_size_ = size;
  }

  /**
   * Set the rejection percentage (percentage to reject from each end)
   *
   * @param percentage Rejection percentage (0.0-0.5)
   */
  void set_reject_percentage(float percentage) {
    reject_percentage_ = percentage;
  }

  /**
   * setup() is called once during ESP32 initialization.
   *
   * This is where we:
   * 1. Create the RobustAverager instance with configured parameters
   * 2. Subscribe to the source sensor's state updates
   */
  void setup() override;

  /**
   * loop() is called repeatedly by ESPHome's main loop.
   *
   * For this event-driven component, loop() is not used. All processing
   * happens in on_sensor_update() callback triggered by source sensor.
   */
  void loop() override {}

protected:
  /**
   * Callback function triggered when source sensor publishes new value
   *
   * @param value The new sensor reading
   */
  void on_sensor_update(float value);

  // Source sensor reference (set from YAML config)
  sensor::Sensor *sensor_source_{nullptr};

  // Configuration parameters (set from YAML config)
  int window_size_{20};              // Default: 20 readings
  float reject_percentage_{0.10f};   // Default: 10% from each end

  // RobustAverager instance (created in setup() with config parameters)
  RobustAverager<float> *averager_{nullptr};
};

} // namespace sensor_filter
} // namespace esphome
