#include "ezo_ph.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cstring>
#include <cmath>

namespace esphome {
namespace ezo_ph {

static const char *const TAG = "ezo_ph";

void EZOPHComponent::setup() {
  ESP_LOGI(TAG, "Setting up EZO pH sensor...");

  // Initialize stability buffer
  memset(this->stability_buffer_, 0, sizeof(this->stability_buffer_));
  this->stability_index_ = 0;
  this->stability_count_ = 0;

  // Test I2C communication with device info command
  if (!this->send_command_("i")) {
    ESP_LOGE(TAG, "Failed to send device info command");
    this->mark_failed();
    return;
  }

  this->wait_for_response_();

  char response[RESPONSE_BUFFER_SIZE];
  if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    ESP_LOGI(TAG, "EZO pH device info: %s", response);
  } else {
    ESP_LOGW(TAG, "Failed to read device info");
  }

  // Lock I2C protocol to prevent accidental switch to UART
  if (this->send_command_("Plock,1")) {
    this->wait_for_response_();
    if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
      if (this->check_response_code_(response)) {
        ESP_LOGD(TAG, "I2C protocol locked");
      }
    }
  }

  // Disable continuous reading mode
  if (!this->send_command_("C,0")) {
    ESP_LOGW(TAG, "Failed to disable continuous reading mode");
  } else {
    this->wait_for_response_();
    if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
      if (this->check_response_code_(response)) {
        ESP_LOGD(TAG, "Continuous reading disabled");
      }
    }
  }

  // Enable verbose response codes
  if (this->send_command_("RESPONSE,1")) {
    this->wait_for_response_();
    if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
      if (this->check_response_code_(response)) {
        ESP_LOGD(TAG, "Verbose responses enabled");
      }
    }
  }

  this->sensor_ready_ = true;
  this->error_count_ = 0;
  ESP_LOGI(TAG, "EZO pH sensor setup complete");
}

void EZOPHComponent::update() {
  if (!this->sensor_ready_) {
    ESP_LOGW(TAG, "Sensor not ready, skipping update");
    return;
  }

  // Send temperature compensation if configured
  if (this->temp_sensor_ != nullptr && this->temp_sensor_->has_state()) {
    float temp = this->temp_sensor_->state;
    char temp_cmd[20];
    snprintf(temp_cmd, sizeof(temp_cmd), "T,%.1f", temp);

    if (this->send_command_(temp_cmd)) {
      this->wait_for_response_();
      char response[RESPONSE_BUFFER_SIZE];
      if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
        if (this->check_response_code_(response)) {
          ESP_LOGV(TAG, "Temperature compensation set to %.1f°C", temp);
        }
      }
    }
  }

  // Request single pH reading
  if (!this->send_command_("R")) {
    ESP_LOGW(TAG, "Failed to send read command");
    this->error_count_++;
    if (this->error_count_ > MAX_ERRORS) {
      ESP_LOGE(TAG, "Too many errors, marking sensor as not ready");
      this->sensor_ready_ = false;
    }
    return;
  }

  // Wait for sensor to process measurement (critical timing)
  this->wait_for_response_();

  // Read response
  char response[RESPONSE_BUFFER_SIZE];
  if (!this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    ESP_LOGW(TAG, "Failed to read pH value");
    this->error_count_++;
    if (this->error_count_ > MAX_ERRORS) {
      ESP_LOGE(TAG, "Too many errors, marking sensor as not ready");
      this->sensor_ready_ = false;
    }
    return;
  }

  // Parse pH value
  float ph_value;
  if (!this->parse_ph_value_(response, ph_value)) {
    ESP_LOGW(TAG, "Failed to parse pH value from response: %s", response);
    this->error_count_++;
    if (this->error_count_ > MAX_ERRORS) {
      ESP_LOGE(TAG, "Too many errors, marking sensor as not ready");
      this->sensor_ready_ = false;
    }
    return;
  }

  // Validate pH range
  if (ph_value < PH_MIN || ph_value > PH_MAX || std::isnan(ph_value)) {
    ESP_LOGW(TAG, "pH value out of range: %.2f", ph_value);
    this->error_count_++;
    if (this->error_count_ > MAX_ERRORS) {
      ESP_LOGE(TAG, "Too many errors, marking sensor as not ready");
      this->sensor_ready_ = false;
    }
    return;
  }

  // Success - reset error counter
  this->error_count_ = 0;

  // Update stability buffer
  this->update_stability_buffer_(ph_value);

  // Publish value to sensor
  if (this->ph_sensor_ != nullptr) {
    this->ph_sensor_->publish_state(ph_value);
    ESP_LOGD(TAG, "pH: %.2f", ph_value);
  }
}

void EZOPHComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "EZO pH Sensor:");
  LOG_I2C_DEVICE(this);
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "pH", this->ph_sensor_);

  if (this->temp_sensor_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Temperature Compensation: Enabled");
  } else {
    ESP_LOGCONFIG(TAG, "  Temperature Compensation: Disabled");
  }

  if (!this->sensor_ready_) {
    ESP_LOGE(TAG, "  Sensor Status: NOT READY (check I2C connection)");
  }
}

// Calibration methods

void EZOPHComponent::calibrate_mid(float ph_value) {
  char cmd[20];
  snprintf(cmd, sizeof(cmd), "Cal,mid,%.2f", ph_value);

  ESP_LOGI(TAG, "Starting mid-point calibration at pH %.2f", ph_value);

  if (!this->send_command_(cmd)) {
    ESP_LOGE(TAG, "Failed to send mid-point calibration command");
    return;
  }

  this->wait_for_response_();

  char response[RESPONSE_BUFFER_SIZE];
  if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    if (this->check_response_code_(response)) {
      ESP_LOGI(TAG, "Mid-point calibration successful at pH %.2f", ph_value);
    } else {
      ESP_LOGE(TAG, "Mid-point calibration failed: %s", response);
    }
  }
}

void EZOPHComponent::calibrate_low(float ph_value) {
  char cmd[20];
  snprintf(cmd, sizeof(cmd), "Cal,low,%.2f", ph_value);

  ESP_LOGI(TAG, "Starting low-point calibration at pH %.2f", ph_value);

  if (!this->send_command_(cmd)) {
    ESP_LOGE(TAG, "Failed to send low-point calibration command");
    return;
  }

  this->wait_for_response_();

  char response[RESPONSE_BUFFER_SIZE];
  if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    if (this->check_response_code_(response)) {
      ESP_LOGI(TAG, "Low-point calibration successful at pH %.2f", ph_value);
    } else {
      ESP_LOGE(TAG, "Low-point calibration failed: %s", response);
    }
  }
}

void EZOPHComponent::calibrate_high(float ph_value) {
  char cmd[20];
  snprintf(cmd, sizeof(cmd), "Cal,high,%.2f", ph_value);

  ESP_LOGI(TAG, "Starting high-point calibration at pH %.2f", ph_value);

  if (!this->send_command_(cmd)) {
    ESP_LOGE(TAG, "Failed to send high-point calibration command");
    return;
  }

  this->wait_for_response_();

  char response[RESPONSE_BUFFER_SIZE];
  if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    if (this->check_response_code_(response)) {
      ESP_LOGI(TAG, "High-point calibration successful at pH %.2f", ph_value);
    } else {
      ESP_LOGE(TAG, "High-point calibration failed: %s", response);
    }
  }
}

void EZOPHComponent::calibrate_clear() {
  ESP_LOGI(TAG, "Clearing all calibration data");

  if (!this->send_command_("Cal,clear")) {
    ESP_LOGE(TAG, "Failed to send clear calibration command");
    return;
  }

  this->wait_for_response_();

  char response[RESPONSE_BUFFER_SIZE];
  if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    if (this->check_response_code_(response)) {
      ESP_LOGI(TAG, "Calibration data cleared successfully");
    } else {
      ESP_LOGE(TAG, "Failed to clear calibration: %s", response);
    }
  }
}

void EZOPHComponent::query_calibration_status() {
  ESP_LOGI(TAG, "Querying calibration status");

  if (!this->send_command_("Cal,?")) {
    ESP_LOGE(TAG, "Failed to send calibration query command");
    return;
  }

  this->wait_for_response_();

  char response[RESPONSE_BUFFER_SIZE];
  if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    ESP_LOGI(TAG, "Calibration status: %s", response);
    // Response format: ?Cal,n where n is 0, 1, 2, or 3 (number of points calibrated)
  }
}

// Advanced features

void EZOPHComponent::set_led(bool enable) {
  const char *cmd = enable ? "L,1" : "L,0";
  ESP_LOGD(TAG, "Setting LED: %s", enable ? "ON" : "OFF");

  if (this->send_command_(cmd)) {
    this->wait_for_response_();
    char response[RESPONSE_BUFFER_SIZE];
    if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
      this->check_response_code_(response);
    }
  }
}

void EZOPHComponent::enter_sleep_mode() {
  ESP_LOGI(TAG, "Entering sleep mode");

  if (this->send_command_("Sleep")) {
    // Note: No response expected after sleep command
    this->sensor_ready_ = false;
  }
}

bool EZOPHComponent::is_stable() {
  if (this->stability_count_ < STABILITY_BUFFER_SIZE) {
    return false;  // Not enough data yet
  }

  float std_dev = this->calculate_std_deviation_();
  return std_dev < STABILITY_THRESHOLD;
}

// I2C communication helpers

bool EZOPHComponent::send_command_(const char *cmd) {
  // EZO expects commands terminated with carriage return
  char cmd_with_cr[RESPONSE_BUFFER_SIZE];
  snprintf(cmd_with_cr, sizeof(cmd_with_cr), "%s\r", cmd);

  ESP_LOGV(TAG, "Sending command: %s", cmd);

  auto err = this->write(reinterpret_cast<const uint8_t *>(cmd_with_cr), strlen(cmd_with_cr));
  if (err != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "I2C write error: %d", err);
    return false;
  }

  return true;
}

bool EZOPHComponent::read_response_(char *buffer, size_t len) {
  memset(buffer, 0, len);

  auto err = this->read(reinterpret_cast<uint8_t *>(buffer), len - 1);
  if (err != i2c::ERROR_OK) {
    ESP_LOGV(TAG, "I2C read error: %d", err);
    return false;
  }

  // Strip trailing whitespace and carriage returns
  size_t actual_len = strlen(buffer);
  while (actual_len > 0 && (buffer[actual_len - 1] == '\r' || buffer[actual_len - 1] == '\n' ||
                             buffer[actual_len - 1] == ' ')) {
    buffer[actual_len - 1] = '\0';
    actual_len--;
  }

  ESP_LOGV(TAG, "Received response: %s", buffer);

  return actual_len > 0;
}

bool EZOPHComponent::parse_ph_value_(const char *response, float &value) {
  if (response == nullptr || strlen(response) == 0) {
    return false;
  }

  // Skip response code if present (e.g., "*OK,6.54" or just "6.54")
  const char *value_start = response;

  // Check for response codes
  if (strncmp(response, "*OK", 3) == 0) {
    // Find comma after *OK
    value_start = strchr(response, ',');
    if (value_start != nullptr) {
      value_start++;  // Skip the comma
    } else {
      // No value after *OK (just confirmation)
      return false;
    }
  } else if (strncmp(response, "*ER", 3) == 0 || strncmp(response, "*OV", 3) == 0 ||
             strncmp(response, "*UV", 3) == 0) {
    // Error responses
    return false;
  }

  // Parse float value
  char *endptr;
  value = strtof(value_start, &endptr);

  // Check if parsing was successful
  if (endptr == value_start || std::isnan(value)) {
    return false;
  }

  return true;
}

bool EZOPHComponent::check_response_code_(const char *response) {
  if (response == nullptr || strlen(response) == 0) {
    return false;
  }

  if (strncmp(response, "*OK", 3) == 0) {
    return true;
  }

  if (strncmp(response, "*ER", 3) == 0) {
    ESP_LOGW(TAG, "EZO error response: %s", response);
    return false;
  }

  if (strncmp(response, "*OV", 3) == 0) {
    ESP_LOGW(TAG, "EZO overvoltage warning: %s", response);
    return false;
  }

  if (strncmp(response, "*UV", 3) == 0) {
    ESP_LOGW(TAG, "EZO undervoltage warning: %s", response);
    return false;
  }

  if (strncmp(response, "*RS", 3) == 0) {
    ESP_LOGW(TAG, "EZO reset detected: %s", response);
    return false;
  }

  // No recognized response code - might be just data
  return true;
}

void EZOPHComponent::wait_for_response_() {
  // Critical timing: EZO circuit requires 300ms to process commands
  delay(RESPONSE_DELAY_MS);
}

// Stability tracking

void EZOPHComponent::update_stability_buffer_(float value) {
  this->stability_buffer_[this->stability_index_] = value;
  this->stability_index_ = (this->stability_index_ + 1) % STABILITY_BUFFER_SIZE;

  if (this->stability_count_ < STABILITY_BUFFER_SIZE) {
    this->stability_count_++;
  }
}

float EZOPHComponent::calculate_std_deviation_() {
  if (this->stability_count_ == 0) {
    return 0.0f;
  }

  // Calculate mean
  float sum = 0.0f;
  for (size_t i = 0; i < this->stability_count_; i++) {
    sum += this->stability_buffer_[i];
  }
  float mean = sum / this->stability_count_;

  // Calculate variance
  float variance_sum = 0.0f;
  for (size_t i = 0; i < this->stability_count_; i++) {
    float diff = this->stability_buffer_[i] - mean;
    variance_sum += diff * diff;
  }
  float variance = variance_sum / this->stability_count_;

  // Return standard deviation
  return sqrt(variance);
}

}  // namespace ezo_ph
}  // namespace esphome
