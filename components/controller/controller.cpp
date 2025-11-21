#include "controller.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cmath>
#include <cstdlib> // for rand()

namespace esphome {
namespace controller {

static const char *TAG = "controller.fsm";

void Controller::setup() {
  this->current_state_ = &Controller::state_init;
  this->state_start_time_ = millis();
  this->state_counter_ = 0;

  // Seed random number generator
  std::srand(millis());

  if (this->sensor_source_ != nullptr) {
    this->sensor_source_->add_on_state_callback([this](float x) {
      this->current_sensor_value_ = x;
    });
  }
}

void Controller::loop() {
  if (this->current_state_ != nullptr) {
    StateFunc next_state = (this->*current_state_)();

    if (next_state.func != this->current_state_) {
        ESP_LOGD(TAG, "State transition.");
        this->state_start_time_ = millis();
        this->state_counter_ = 0; // Reset counter for the new state
        this->current_state_ = next_state.func;
    }
  }
}

// --- State: INIT (Red -> Yellow -> Green) ---
Controller::StateFunc Controller::state_init() {
  uint32_t elapsed = millis() - this->state_start_time_;

  if (elapsed < 1000) {
    apply_light(1.0, 0.0, 0.0); // Red
  } else if (elapsed < 2000) {
    apply_light(1.0, 1.0, 0.0); // Yellow
  } else if (elapsed < 3000) {
    apply_light(0.0, 1.0, 0.0); // Green
  } else {
    return &Controller::state_calibration;
  }

  return &Controller::state_init;
}

// --- State: CALIBRATION (Blink Yellow) ---
Controller::StateFunc Controller::state_calibration() {
  uint32_t elapsed = millis() - this->state_start_time_;

  bool on = (elapsed / 500) % 2 == 0;
  if (on) apply_light(1.0, 0.8, 0.0, 1.0);
  else    apply_light(0.0, 0.0, 0.0, 0.0);

  if (elapsed > 4000) {
    return &Controller::state_ready;
  }

  return &Controller::state_calibration;
}

// --- State: READY (Breathing Green, Occasional Error) ---
Controller::StateFunc Controller::state_ready() {
  uint32_t elapsed = millis() - this->state_start_time_;
  uint32_t current_seconds = elapsed / 1000;

  // 1. Stochastic Transition Logic
  // Check only once per second (when the second counter increments)
  if (current_seconds > this->state_counter_) {
    this->state_counter_ = current_seconds;

    // 5% chance to fail every second
    // This ensures we stay in Ready "most of the time" (avg 20s)
    if ((std::rand() % 100) < 5) {
       ESP_LOGW(TAG, "Random error triggered!");
       return &Controller::state_error;
    }
  }

  // 2. Visual Logic: Breathing Green
  float t = millis() / 1000.0f;
  float brightness = (std::sin(t * 3.14159f) + 1.0f) / 2.0f; // 0.0 to 1.0
  brightness = 0.1f + (brightness * 0.9f); // Min brightness 10%

  apply_light(0.0, 1.0, 0.0, brightness);

  return &Controller::state_ready;
}

// --- State: ERROR (Flash Red, then Re-Init) ---
Controller::StateFunc Controller::state_error() {
  uint32_t elapsed = millis() - this->state_start_time_;

  // Remain in error for 5 seconds ("a bit longer")
  if (elapsed > 5000) {
    ESP_LOGI(TAG, "Error cleared. Re-initializing...");
    return &Controller::state_init; // Re-init
  }

  // Fast Flash Red
  bool on = (elapsed / 100) % 2 == 0;
  if (on) apply_light(1.0, 0.0, 0.0, 1.0);
  else    apply_light(0.0, 0.0, 0.0, 0.0);

  return &Controller::state_error;
}

void Controller::apply_light(float r, float g, float b, float brightness) {
  if (this->light_target_ == nullptr) return;

  auto call = this->light_target_->make_call();
  call.set_state(brightness > 0.01f);
  call.set_brightness(brightness);
  call.set_rgb(r, g, b);
  call.perform();
}

} // namespace controller
} // namespace esphome
