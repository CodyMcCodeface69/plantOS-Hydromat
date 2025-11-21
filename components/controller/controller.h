#pragma once
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/light/light_state.h"

namespace esphome {
namespace controller {

class Controller : public Component {
 public:
  void setup() override;
  void loop() override; // FSM Driver

  void set_sensor_source(sensor::Sensor *s) { sensor_source_ = s; }
  void set_light_target(light::LightState *l) { light_target_ = l; }

 private:
  sensor::Sensor *sensor_source_{nullptr};
  light::LightState *light_target_{nullptr};

  float current_sensor_value_{0.0};

  // --- FSM Infrastructure ---
  struct StateFunc;
  using StateHandler = StateFunc (Controller::*)();

  struct StateFunc {
    StateHandler func;
    StateFunc(StateHandler f) : func(f) {}
  };

  StateHandler current_state_{nullptr};
  uint32_t state_start_time_{0};

  // Counts 'events' within a state (e.g. seconds passed) to avoid repeated triggers
  uint32_t state_counter_{0};

  // --- State Functions ---
  StateFunc state_init();
  StateFunc state_calibration();
  StateFunc state_ready();
  StateFunc state_error();

  void apply_light(float r, float g, float b, float brightness = 1.0);
};

} // namespace controller
} // namespace esphome
