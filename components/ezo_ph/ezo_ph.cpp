#include "ezo_ph.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cstring>
#include <cmath>

namespace esphome {
namespace ezo_ph {

static const char *const TAG = "ezo_ph";

void EZOPHComponent::setup() {
  ESP_LOGI(TAG, "Setting up EZO pH sensor (address 0x%02X)...", this->address_);

  // Initialize stability buffer
  memset(this->stability_buffer_, 0, sizeof(this->stability_buffer_));
  this->stability_index_ = 0;
  this->stability_count_ = 0;
  this->update_state_ = UpdateState::IDLE;

  // Test I2C communication with device info command
  if (!this->send_command_("i")) {
    ESP_LOGE(TAG, "Failed to reach EZO pH at 0x%02X — will retry in %u ms "
                  "(check wiring, pull-ups 4.7kΩ to 3.3V, and I2C address)",
             this->address_, SETUP_RETRY_INTERVAL_MS);
    this->sensor_ready_ = false;
    this->next_setup_retry_ms_ = millis() + SETUP_RETRY_INTERVAL_MS;
    return;
  }

  this->wait_blocking_();

  char response[RESPONSE_BUFFER_SIZE];
  if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    ESP_LOGI(TAG, "EZO pH device info: %s", response);
  } else {
    ESP_LOGW(TAG, "No response to 'i' command (device may be initializing)");
  }

  // Lock I2C protocol to prevent accidental switch to UART
  if (this->send_command_("Plock,1")) {
    this->wait_blocking_();
    if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
      if (this->check_response_code_(response)) {
        ESP_LOGI(TAG, "I2C protocol locked (Plock,1 confirmed)");
      }
    }
  }

  // Disable continuous reading mode
  if (!this->send_command_("C,0")) {
    ESP_LOGW(TAG, "Failed to disable continuous reading mode");
  } else {
    this->wait_blocking_();
    if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
      if (this->check_response_code_(response)) {
        ESP_LOGD(TAG, "Continuous reading disabled");
      }
    }
  }

  this->sensor_ready_ = true;
  this->error_count_ = 0;
  this->next_setup_retry_ms_ = 0;
  ESP_LOGI(TAG, "EZO pH sensor ready at I2C 0x%02X", this->address_);
}

void EZOPHComponent::update() {
  if (!this->sensor_ready_) {
    ESP_LOGW(TAG, "Sensor not ready, skipping update");
    return;
  }

  // Don't start a new cycle if one is already in progress
  if (this->update_state_ != UpdateState::IDLE) return;

  // Send temperature compensation if configured
  if (this->temp_sensor_ != nullptr && this->temp_sensor_->has_state()) {
    char cmd[20];
    snprintf(cmd, sizeof(cmd), "T,%.1f", this->temp_sensor_->state);
    if (this->send_command_(cmd)) {
      this->response_wait_start_ = millis();
      this->update_state_ = UpdateState::WAITING_TEMP_COMP;
      return;
    }
    // Fall through to read without temp comp on send failure
  }

  this->start_ph_read_();
}

void EZOPHComponent::loop() {
  // Auto-retry setup if it failed at boot or after too many errors
  if (!this->sensor_ready_ && this->next_setup_retry_ms_ != 0 &&
      millis() >= this->next_setup_retry_ms_) {
    this->next_setup_retry_ms_ = 0;
    ESP_LOGI(TAG, "Retrying EZO pH setup (attempt %u)...", ++this->setup_retry_count_);
    this->setup();
    return;
  }

  if (this->update_state_ == UpdateState::IDLE) return;

  if (millis() - this->response_wait_start_ < RESPONSE_DELAY_MS) return;

  char response[RESPONSE_BUFFER_SIZE];

  if (this->update_state_ == UpdateState::WAITING_TEMP_COMP) {
    this->read_response_(response, RESPONSE_BUFFER_SIZE);  // consume T,? response
    this->start_ph_read_();
    return;
  }

  if (this->update_state_ == UpdateState::WAITING_READ) {
    if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
      float ph_value;
      if (this->parse_ph_value_(response, ph_value) &&
          ph_value >= PH_MIN && ph_value <= PH_MAX && !std::isnan(ph_value)) {
        this->error_count_ = 0;
        this->last_ph_value_ = ph_value;
        this->update_stability_buffer_(ph_value);
        if (this->ph_sensor_) this->ph_sensor_->publish_state(ph_value);
        ESP_LOGD(TAG, "pH: %.2f", ph_value);
      } else {
        this->handle_error_("Invalid pH reading");
      }
    } else {
      this->handle_error_("Read failed");
    }
    this->update_state_ = UpdateState::IDLE;
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

// ============================================================================
// Non-blocking helpers
// ============================================================================

void EZOPHComponent::start_ph_read_() {
  if (this->send_command_("R")) {
    this->response_wait_start_ = millis();
    this->update_state_ = UpdateState::WAITING_READ;
  } else {
    this->handle_error_("Failed to send R command");
    this->update_state_ = UpdateState::IDLE;
  }
}

void EZOPHComponent::handle_error_(const char *msg) {
  ESP_LOGW(TAG, "%s (error %u/%u)", msg, this->error_count_ + 1, MAX_ERRORS);
  if (++this->error_count_ > MAX_ERRORS) {
    ESP_LOGE(TAG, "Too many consecutive errors — marking not ready, will retry in %u ms",
             SETUP_RETRY_INTERVAL_MS);
    this->sensor_ready_ = false;
    this->next_setup_retry_ms_ = millis() + SETUP_RETRY_INTERVAL_MS;
  }
}

// ============================================================================
// Calibration methods (blocking — called rarely from web UI / calibration FSM)
// ============================================================================

void EZOPHComponent::calibrate_mid(float ph_value) {
  char cmd[20];
  snprintf(cmd, sizeof(cmd), "Cal,mid,%.2f", ph_value);
  ESP_LOGI(TAG, "Mid-point calibration at pH %.2f", ph_value);
  if (!this->send_command_(cmd)) { ESP_LOGE(TAG, "Failed to send mid calibration command"); return; }
  this->wait_blocking_();
  char response[RESPONSE_BUFFER_SIZE];
  if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    if (this->check_response_code_(response)) {
      ESP_LOGI(TAG, "Mid-point calibration successful");
    } else {
      ESP_LOGE(TAG, "Mid-point calibration failed: %s", response);
    }
  }
}

void EZOPHComponent::calibrate_low(float ph_value) {
  char cmd[20];
  snprintf(cmd, sizeof(cmd), "Cal,low,%.2f", ph_value);
  ESP_LOGI(TAG, "Low-point calibration at pH %.2f", ph_value);
  if (!this->send_command_(cmd)) { ESP_LOGE(TAG, "Failed to send low calibration command"); return; }
  this->wait_blocking_();
  char response[RESPONSE_BUFFER_SIZE];
  if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    if (this->check_response_code_(response)) {
      ESP_LOGI(TAG, "Low-point calibration successful");
    } else {
      ESP_LOGE(TAG, "Low-point calibration failed: %s", response);
    }
  }
}

void EZOPHComponent::calibrate_high(float ph_value) {
  char cmd[20];
  snprintf(cmd, sizeof(cmd), "Cal,high,%.2f", ph_value);
  ESP_LOGI(TAG, "High-point calibration at pH %.2f", ph_value);
  if (!this->send_command_(cmd)) { ESP_LOGE(TAG, "Failed to send high calibration command"); return; }
  this->wait_blocking_();
  char response[RESPONSE_BUFFER_SIZE];
  if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    if (this->check_response_code_(response)) {
      ESP_LOGI(TAG, "High-point calibration successful");
    } else {
      ESP_LOGE(TAG, "High-point calibration failed: %s", response);
    }
  }
}

void EZOPHComponent::calibrate_clear() {
  ESP_LOGI(TAG, "Clearing all calibration data");
  if (!this->send_command_("Cal,clear")) { ESP_LOGE(TAG, "Failed to send clear calibration command"); return; }
  this->wait_blocking_();
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
  if (!this->send_command_("Cal,?")) { ESP_LOGE(TAG, "Failed to send calibration query"); return; }
  this->wait_blocking_();
  char response[RESPONSE_BUFFER_SIZE];
  if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    ESP_LOGI(TAG, "Calibration status: %s", response);
    // Response format: ?Cal,n where n = 0/1/2/3 (number of calibration points)
  }
}

// ============================================================================
// Additional interface methods (ezo_ph_uart compatibility)
// ============================================================================

bool EZOPHComponent::take_single_reading(float &value) {
  // Blocking single read used during calibration sequence (rare). Blocks ~300ms.
  if (!this->sensor_ready_) return false;
  if (!this->send_command_("R")) return false;
  this->wait_blocking_();
  char response[RESPONSE_BUFFER_SIZE];
  if (!this->read_response_(response, RESPONSE_BUFFER_SIZE)) return false;
  return this->parse_ph_value_(response, value);
}

bool EZOPHComponent::send_temperature_compensation(float temperature) {
  // Explicit blocking temperature compensation. Normal path uses update() auto-comp.
  char cmd[20];
  snprintf(cmd, sizeof(cmd), "T,%.1f", temperature);
  if (!this->send_command_(cmd)) return false;
  this->wait_blocking_();
  char response[RESPONSE_BUFFER_SIZE];
  this->read_response_(response, RESPONSE_BUFFER_SIZE);
  return this->check_response_code_(response);
}

void EZOPHComponent::switch_to_uart_mode() {
  // Sends "Baud,9600" over I2C. EZO immediately self-resets in UART mode.
  // No response is expected — the sensor stops responding on I2C after this.
  ESP_LOGW(TAG, "Switching EZO pH to UART mode (Baud,9600) — sensor will reset");
  this->send_command_("Baud,9600");
  this->sensor_ready_ = false;
  this->update_state_ = UpdateState::IDLE;
  ESP_LOGW(TAG, "EZO pH is now in UART mode at 9600 baud. Reconfigure firmware to use UART.");
}

// ============================================================================
// Diagnostic methods (blocking — called on demand from web UI buttons only)
// ============================================================================

void EZOPHComponent::request_device_info() {
  ESP_LOGI(TAG, "Requesting device info...");
  if (!this->send_command_("i")) {
    ESP_LOGE(TAG, "Failed to send 'i' command");
    return;
  }
  this->wait_blocking_();
  char response[RESPONSE_BUFFER_SIZE];
  if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    ESP_LOGI(TAG, "Device info: %s", response);
  } else {
    ESP_LOGW(TAG, "No response to 'i' command (device not responding)");
  }
}

void EZOPHComponent::request_status() {
  ESP_LOGI(TAG, "Requesting device status...");
  if (!this->send_command_("Status")) {
    ESP_LOGE(TAG, "Failed to send 'Status' command");
    return;
  }
  this->wait_blocking_();
  char response[RESPONSE_BUFFER_SIZE];
  if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    ESP_LOGI(TAG, "Device status: %s", response);  // e.g. ?Status,P,5.038 (P=power restored)
  } else {
    ESP_LOGW(TAG, "No response to 'Status' command");
  }
}

void EZOPHComponent::force_setup_retry() {
  ESP_LOGI(TAG, "Forcing immediate setup retry...");
  this->sensor_ready_ = false;
  this->update_state_ = UpdateState::IDLE;
  this->error_count_ = 0;
  this->next_setup_retry_ms_ = millis() + 500;
}

// ============================================================================
// Advanced features
// ============================================================================

void EZOPHComponent::set_led(bool enable) {
  const char *cmd = enable ? "L,1" : "L,0";
  ESP_LOGD(TAG, "Setting LED: %s", enable ? "ON" : "OFF");
  if (this->send_command_(cmd)) {
    this->wait_blocking_();
    char response[RESPONSE_BUFFER_SIZE];
    if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
      this->check_response_code_(response);
    }
  }
}

void EZOPHComponent::enter_sleep_mode() {
  ESP_LOGI(TAG, "Entering sleep mode");
  if (this->send_command_("Sleep")) {
    this->sensor_ready_ = false;
  }
}

bool EZOPHComponent::is_stable() {
  if (this->stability_count_ < STABILITY_BUFFER_SIZE) return false;
  return this->calculate_std_deviation_() < STABILITY_THRESHOLD;
}

// ============================================================================
// I2C communication helpers
// ============================================================================

bool EZOPHComponent::send_command_(const char *cmd) {
  // I2C mode: NO carriage return — Atlas Scientific EZO I2C protocol sends raw ASCII bytes only.
  // (UART mode used \r, I2C does not)
  ESP_LOGV(TAG, "Sending command: %s", cmd);
  auto err = this->write(reinterpret_cast<const uint8_t *>(cmd), strlen(cmd));
  if (err != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "I2C write error %d on cmd '%s' (addr 0x%02X) — "
                  "check pull-ups (4.7kΩ to 3.3V required) and wiring",
             err, cmd, this->address_);
    return false;
  }
  return true;
}

bool EZOPHComponent::read_response_(char *buffer, size_t len) {
  memset(buffer, 0, len);
  auto err = this->read(reinterpret_cast<uint8_t *>(buffer), len - 1);
  if (err != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "I2C read error %d (addr 0x%02X)", err, this->address_);
    return false;
  }
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
  if (response == nullptr || strlen(response) == 0) return false;

  const char *value_start = response;
  if (strncmp(response, "*OK", 3) == 0) {
    value_start = strchr(response, ',');
    if (value_start != nullptr) {
      value_start++;
    } else {
      return false;
    }
  } else if (strncmp(response, "*ER", 3) == 0 || strncmp(response, "*OV", 3) == 0 ||
             strncmp(response, "*UV", 3) == 0) {
    return false;
  }

  char *endptr;
  value = strtof(value_start, &endptr);
  if (endptr == value_start || std::isnan(value)) return false;
  return true;
}

bool EZOPHComponent::check_response_code_(const char *response) {
  if (response == nullptr || strlen(response) == 0) return false;
  if (strncmp(response, "*OK", 3) == 0) return true;
  if (strncmp(response, "*ER", 3) == 0) { ESP_LOGW(TAG, "EZO error: %s", response); return false; }
  if (strncmp(response, "*OV", 3) == 0) { ESP_LOGW(TAG, "EZO overvoltage: %s", response); return false; }
  if (strncmp(response, "*UV", 3) == 0) { ESP_LOGW(TAG, "EZO undervoltage: %s", response); return false; }
  if (strncmp(response, "*RS", 3) == 0) { ESP_LOGW(TAG, "EZO reset: %s", response); return false; }
  return true;
}

void EZOPHComponent::wait_blocking_() {
  // Blocking 300ms wait — only used in setup() and calibration paths (called rarely)
  delay(RESPONSE_DELAY_MS);
}

// ============================================================================
// Stability tracking
// ============================================================================

void EZOPHComponent::update_stability_buffer_(float value) {
  this->stability_buffer_[this->stability_index_] = value;
  this->stability_index_ = (this->stability_index_ + 1) % STABILITY_BUFFER_SIZE;
  if (this->stability_count_ < STABILITY_BUFFER_SIZE) this->stability_count_++;
}

float EZOPHComponent::calculate_std_deviation_() {
  if (this->stability_count_ == 0) return 0.0f;
  float sum = 0.0f;
  for (size_t i = 0; i < this->stability_count_; i++) sum += this->stability_buffer_[i];
  float mean = sum / this->stability_count_;
  float variance_sum = 0.0f;
  for (size_t i = 0; i < this->stability_count_; i++) {
    float diff = this->stability_buffer_[i] - mean;
    variance_sum += diff * diff;
  }
  return sqrt(variance_sum / this->stability_count_);
}

}  // namespace ezo_ph
}  // namespace esphome
