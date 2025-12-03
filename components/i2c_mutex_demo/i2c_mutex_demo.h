#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

namespace esphome {
namespace i2c_mutex_demo {

static const char *const TAG = "i2c_mutex_demo";

class I2CMutexDemo : public Component {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // Configuration setter
  void set_test_mode(bool test_mode) { this->test_mode_ = test_mode; }

  // Simulates a time-consuming I²C operation
  void performI2CTransaction(const char *task_name);

  // Static task functions for FreeRTOS
  static void taskA(void *pvParameters);
  static void taskB(void *pvParameters);

  // Global mutex for I²C bus protection (accessible to other components)
  static SemaphoreHandle_t i2c_mutex_;

  // Singleton instance pointer for static task functions
  static I2CMutexDemo *instance_;

 private:
  bool test_mode_ = false;
  TaskHandle_t task_a_handle_ = nullptr;
  TaskHandle_t task_b_handle_ = nullptr;
};

}  // namespace i2c_mutex_demo
}  // namespace esphome
