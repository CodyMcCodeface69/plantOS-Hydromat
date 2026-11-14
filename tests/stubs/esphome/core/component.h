// Host stub for esphome/core/component.h.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

namespace esphome {

namespace setup_priority {
constexpr float BUS = 1000.0f;
constexpr float IO = 900.0f;
constexpr float HARDWARE = 800.0f;
constexpr float DATA = 600.0f;
constexpr float PROCESSOR = 400.0f;
constexpr float WIFI = 250.0f;
constexpr float AFTER_WIFI = 200.0f;
constexpr float LATE = -100.0f;
}  // namespace setup_priority

class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return setup_priority::DATA; }
  void mark_failed() { failed_ = true; }
  bool is_failed() const { return failed_; }

 protected:
  bool failed_{false};
};

class PollingComponent : public Component {
 public:
  PollingComponent() = default;
  explicit PollingComponent(uint32_t interval) : interval_(interval) {}
  virtual void update() {}
  void set_update_interval(uint32_t interval) { interval_ = interval; }
  uint32_t get_update_interval() const { return interval_; }

 protected:
  uint32_t interval_{0};
};

}  // namespace esphome
