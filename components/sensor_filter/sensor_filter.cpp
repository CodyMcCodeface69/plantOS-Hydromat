#include "sensor_filter.h"

namespace esphome {
namespace sensor_filter {

// Tag for ESP logging
static const char *TAG = "sensor_filter";

void SensorFilter::setup() {
  /**
   * Initialize the SensorFilter component.
   *
   * SETUP TASKS:
   * 1. Create RobustAverager instance with configured parameters
   * 2. Subscribe to source sensor's state updates
   * 3. Log configuration for debugging
   *
   * WHY SUBSCRIBE IN SETUP:
   * ESPHome components must register callbacks during setup() to ensure
   * they're ready before sensor updates begin. If we delayed subscription,
   * we might miss early sensor readings.
   */

  // Validate that sensor_source was configured
  if (sensor_source_ == nullptr) {
    ESP_LOGE(TAG, "sensor_source not configured - filter will not work!");
    return;
  }

  // Create RobustAverager instance with configured parameters
  // Note: Using 'new' here - memory is freed in destructor (if needed)
  averager_ = new RobustAverager<float>(window_size_, reject_percentage_);

  ESP_LOGI(TAG, "Sensor filter initialized:");
  ESP_LOGI(TAG, "  Window size: %d readings", window_size_);
  ESP_LOGI(TAG, "  Reject percentage: %.1f%% from each end", reject_percentage_ * 100);
  ESP_LOGI(TAG, "  Total outlier rejection: %.1f%%", reject_percentage_ * 2 * 100);

  // Subscribe to source sensor updates using lambda callback
  // When source sensor calls publish_state(), our on_sensor_update() is called
  sensor_source_->add_on_state_callback([this](float value) {
    this->on_sensor_update(value);
  });

  ESP_LOGD(TAG, "Subscribed to source sensor updates");
}

void SensorFilter::on_sensor_update(float value) {
  /**
   * Callback triggered when source sensor publishes a new value.
   *
   * ISR-SAFE DESIGN:
   * This callback may be called from ISR context (sensor ADC/UART interrupts).
   * We CANNOT use ESP_LOG* functions here as they use FreeRTOS queues which
   * can cause alignment crashes on RISC-V (ESP32-C6).
   * Instead, we set volatile flags and defer logging to loop().
   *
   * PROCESSING WORKFLOW:
   * 1. Add new reading to RobustAverager buffer
   * 2. Check if buffer is full (enough readings collected)
   * 3. If full: Calculate robust average and publish filtered value
   * 4. Set flags for deferred logging in loop()
   * 5. Buffer automatically resets after calculation
   */

  // Skip invalid sensor readings (NaN indicates sensor error/not ready)
  if (std::isnan(value)) {
    has_nan_reading_ = true;  // Flag for deferred logging
    return;
  }

  // Add reading to averager buffer
  averager_->addReading(value);

  // Store values for deferred logging in loop()
  last_raw_value_ = value;
  last_buffer_count_ = averager_->getCount();
  has_new_reading_ = true;

  // Check if we have enough readings to calculate average
  if (averager_->isReady()) {
    /**
     * Buffer is full - calculate robust average and publish.
     *
     * getRobustAverage() performs:
     * 1. Sort all readings
     * 2. Reject configured percentage from each end (outliers)
     * 3. Calculate average of middle values
     * 4. Auto-reset buffer for next window
     */
    float filtered_value = averager_->getRobustAverage();

    /**
     * Publish the filtered value.
     *
     * This makes the filtered value available to:
     * - Other ESPHome components (controllers, automations)
     * - Home Assistant
     * - Web interface
     * - MQTT (if configured)
     *
     * Components subscribing to THIS filter will receive the robust average,
     * not the raw noisy data from the source sensor.
     */
    publish_state(filtered_value);

    // Store for deferred logging
    last_filtered_value_ = filtered_value;
    last_readings_count_ = window_size_;
    has_filtered_value_ = true;
  }
}

void SensorFilter::loop() {
  /**
   * ISR-safe deferred logging.
   *
   * Sensor callbacks (on_sensor_update) can be called from ISR context,
   * where logging is unsafe. We handle all logging here in the main loop.
   */

  // Handle NaN readings
  if (has_nan_reading_) {
    has_nan_reading_ = false;
    ESP_LOGW(TAG, "Received NaN from source sensor - skipping");
  }

  // Handle new raw readings
  if (has_new_reading_) {
    has_new_reading_ = false;
    ESP_LOGD(TAG, "Raw reading: %.2f  [Buffer: %d/%d]",
             last_raw_value_,
             last_buffer_count_,
             window_size_);
  }

  // Handle filtered value calculations
  if (has_filtered_value_) {
    has_filtered_value_ = false;
    ESP_LOGI(TAG, "Robust average calculated: %.2f (from %d readings, %.0f%% outlier rejection)",
             last_filtered_value_,
             last_readings_count_,
             reject_percentage_ * 2 * 100);
    ESP_LOGD(TAG, "Buffer reset. Collecting next window...");
  }
}

} // namespace sensor_filter
} // namespace esphome
