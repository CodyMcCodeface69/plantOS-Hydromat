#pragma once
#include "esphome.h"

namespace esphome {
namespace sensor_dummy {

class SensorDummy : public sensor::Sensor, public PollingComponent {
public:
  // update() is called every 'update_interval' specified in YAML
  void update() override;
  void setup() override;
};

} // namespace sensor_dummy
} // namespace esphome
