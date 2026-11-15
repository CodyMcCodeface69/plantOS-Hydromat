// Host stub for esphome/components/sensor/sensor.h: a value holder that records publishes.
#pragma once
#include <cmath>
#include <vector>

#ifndef LOG_SENSOR
#define LOG_SENSOR(prefix, type, obj)
#endif
#ifndef LOG_UPDATE_INTERVAL
#define LOG_UPDATE_INTERVAL(this)
#endif

namespace esphome {
namespace sensor {

class Sensor {
 public:
  float state{NAN};
  std::vector<float> published;
  void publish_state(float v) {
    state = v;
    has_state_ = true;
    published.push_back(v);
  }
  bool has_state() const { return has_state_; }
  void set_test_state(float v) {
    state = v;
    has_state_ = true;
  }

 private:
  bool has_state_{false};
};

}  // namespace sensor
}  // namespace esphome
