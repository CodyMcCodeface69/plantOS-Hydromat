#include "i2c_mutex_demo.h"

namespace esphome {
namespace i2c_mutex_demo {

// Initialize static members
SemaphoreHandle_t I2CMutexDemo::i2c_mutex_ = nullptr;
I2CMutexDemo *I2CMutexDemo::instance_ = nullptr;

void I2CMutexDemo::setup() {
  ESP_LOGI(TAG, "Setting up I²C Mutex Protection (test_mode: %s)...", this->test_mode_ ? "ENABLED" : "DISABLED");

  // Store instance for static task functions
  instance_ = this;

  // ALWAYS create FreeRTOS Mutex for I²C bus protection
  // This mutex is available for all components that need I²C access
  i2c_mutex_ = xSemaphoreCreateMutex();

  if (i2c_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create I²C Mutex!");
    return;
  }

  ESP_LOGI(TAG, "I²C Mutex created successfully (available globally for I²C protection)");

  // Only create test tasks if test_mode is enabled
  if (this->test_mode_) {
    ESP_LOGI(TAG, "Test mode ENABLED - creating demonstration tasks...");

    // Create Task A on Core 0
    BaseType_t result_a = xTaskCreatePinnedToCore(
        taskA,              // Task function
        "Task_A",           // Task name
        4096,               // Stack size (bytes)
        this,               // Parameters (pass this pointer)
        1,                  // Priority
        &task_a_handle_,    // Task handle
        0                   // Core ID (0)
    );

    if (result_a == pdPASS) {
      ESP_LOGI(TAG, "Task_A created successfully on Core 0");
    } else {
      ESP_LOGE(TAG, "Failed to create Task_A!");
    }

    // Create Task B on Core 0 (same core to demonstrate mutex protection)
    BaseType_t result_b = xTaskCreatePinnedToCore(
        taskB,              // Task function
        "Task_B",           // Task name
        4096,               // Stack size (bytes)
        this,               // Parameters (pass this pointer)
        1,                  // Priority
        &task_b_handle_,    // Task handle
        0                   // Core ID (0)
    );

    if (result_b == pdPASS) {
      ESP_LOGI(TAG, "Task_B created successfully on Core 0");
    } else {
      ESP_LOGE(TAG, "Failed to create Task_B!");
    }

    ESP_LOGI(TAG, "Test mode setup complete - both test tasks running");
  } else {
    ESP_LOGI(TAG, "Test mode DISABLED - mutex available but no test tasks created");
    ESP_LOGI(TAG, "Production mode: Use I2CMutexDemo::i2c_mutex_ in your I²C components");
  }
}

void I2CMutexDemo::loop() {
  // Main loop is empty - all work done in FreeRTOS tasks
  // This demonstrates that FreeRTOS tasks run independently
}

void I2CMutexDemo::performI2CTransaction(const char *task_name) {
  // Simulate a time-consuming I²C operation
  ESP_LOGI(TAG, "[%s] Performing I²C transaction (50ms delay)...", task_name);
  vTaskDelay(pdMS_TO_TICKS(50));  // Simulate I²C transaction time
  ESP_LOGI(TAG, "[%s] I²C transaction completed", task_name);
}

void I2CMutexDemo::taskA(void *pvParameters) {
  I2CMutexDemo *demo = static_cast<I2CMutexDemo *>(pvParameters);
  const char *task_name = "Task_A";

  ESP_LOGI(TAG, "[%s] Started and running", task_name);

  while (true) {
    ESP_LOGI(TAG, "[%s] Attempting to acquire I²C Mutex...", task_name);

    // Try to acquire the mutex (wait indefinitely)
    if (xSemaphoreTake(i2c_mutex_, portMAX_DELAY) == pdTRUE) {
      // Mutex acquired - we have exclusive access to I²C bus
      ESP_LOGI(TAG, "[%s] ✓ Mutex ACQUIRED - entering critical section", task_name);

      // Perform I²C transaction (critical section)
      demo->performI2CTransaction(task_name);

      // Release the mutex
      xSemaphoreGive(i2c_mutex_);
      ESP_LOGI(TAG, "[%s] ✓ Mutex RELEASED - exiting critical section", task_name);
    } else {
      ESP_LOGW(TAG, "[%s] Failed to acquire mutex!", task_name);
    }

    // Wait 1 second before next attempt
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void I2CMutexDemo::taskB(void *pvParameters) {
  I2CMutexDemo *demo = static_cast<I2CMutexDemo *>(pvParameters);
  const char *task_name = "Task_B";

  ESP_LOGI(TAG, "[%s] Started and running", task_name);

  // Small initial delay to offset from Task_A
  vTaskDelay(pdMS_TO_TICKS(300));

  while (true) {
    ESP_LOGI(TAG, "[%s] Attempting to acquire I²C Mutex...", task_name);

    // Try to acquire the mutex (wait indefinitely)
    if (xSemaphoreTake(i2c_mutex_, portMAX_DELAY) == pdTRUE) {
      // Mutex acquired - we have exclusive access to I²C bus
      ESP_LOGI(TAG, "[%s] ✓ Mutex ACQUIRED - entering critical section", task_name);

      // Perform I²C transaction (critical section)
      demo->performI2CTransaction(task_name);

      // Release the mutex
      xSemaphoreGive(i2c_mutex_);
      ESP_LOGI(TAG, "[%s] ✓ Mutex RELEASED - exiting critical section", task_name);
    } else {
      ESP_LOGW(TAG, "[%s] Failed to acquire mutex!", task_name);
    }

    // Wait 1 second before next attempt
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

}  // namespace i2c_mutex_demo
}  // namespace esphome
