#include "sensor_dummy.h"

namespace esphome {
namespace sensor_dummy {

void SensorDummy::setup() {
  // Initialize the sensor value
  publish_state(0);
}

void SensorDummy::update() {
  // Example: Increment value by 10, reset if over 100
  float current = this->state;
  float next = current + 10.0;
  if (next > 100.0) {
    next = 0.0;
  }

  ESP_LOGD("sensor_dummy", "Updating dummy sensor value to: %f", next);
  publish_state(next);
}

} // namespace sensor_dummy
} // namespace esphome
