// Host stub for esphome/components/number/number.h: state + publish_state only.
#pragma once
#include <cmath>

namespace esphome {
namespace number {

class Number {
 public:
  float state{NAN};
  void publish_state(float value) { state = value; }
};

}  // namespace number
}  // namespace esphome
