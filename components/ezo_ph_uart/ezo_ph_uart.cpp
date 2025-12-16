#include "ezo_ph_uart.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cstring>
#include <cmath>

namespace esphome {
namespace ezo_ph_uart {

static const char *const TAG = "ezo_ph_uart";

// ============================================================================
// Component Lifecycle Methods
// ============================================================================

void EZOPHUARTComponent::setup() {
  ESP_LOGI(TAG, "Setting up EZO pH sensor via UART...");

  // Initialize stability tracking buffer
  memset(this->stability_buffer_, 0, sizeof(this->stability_buffer_));
  this->stability_index_ = 0;
  this->stability_count_ = 0;

  // Clear any pending data in UART buffer from power-up
  this->flush();

  // CRITICAL: Disable verbose responses first
  // This makes the sensor respond with just data instead of "*OK,data"
  ESP_LOGI(TAG, "Configuring response format...");
  if (this->send_command_("RESPONSE,0")) {
    this->wait_for_response_();
    char response[RESPONSE_BUFFER_SIZE];
    if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
      ESP_LOGD(TAG, "Response format set to data-only mode: %s", response);
    }
    // Flush any remaining data
    this->flush();
    delay(100);  // Give sensor time to switch modes
  }

  // Test UART communication with device info command
  if (!this->send_command_("i")) {
    ESP_LOGE(TAG, "Failed to send device info command");
    this->mark_failed();
    return;
  }

  // Wait for sensor to process command
  this->wait_for_response_();

  // Read and log device information
  char response[RESPONSE_BUFFER_SIZE];
  if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    ESP_LOGI(TAG, "EZO pH device info: %s", response);
  } else {
    ESP_LOGW(TAG, "Failed to read device info - check UART connection");
  }

  // Disable continuous reading mode (we use polling instead)
  if (!this->send_command_("C,0")) {
    ESP_LOGW(TAG, "Failed to disable continuous reading mode");
  } else {
    this->wait_for_response_();
    if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
      ESP_LOGD(TAG, "Continuous reading mode disabled: %s", response);
    }
  }

  // Sensor is ready for operation
  this->sensor_ready_ = true;
  this->error_count_ = 0;
  ESP_LOGI(TAG, "EZO pH sensor setup complete");
}

void EZOPHUARTComponent::update() {
  if (!this->sensor_ready_) {
    ESP_LOGW(TAG, "Sensor not ready, skipping update");
    return;
  }

  // ========================================
  // Continuous Reading Mode
  // ========================================
  if (this->continuous_mode_active_) {
    // In continuous mode, sensor automatically outputs pH every second
    // We just read whatever is available in the buffer

    // Check if data is available
    if (!this->available()) {
      ESP_LOGV(TAG, "Continuous mode: No data available yet");
      return;
    }

    // Read the automatically sent pH value
    char response[RESPONSE_BUFFER_SIZE];
    if (!this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
      ESP_LOGV(TAG, "Continuous mode: Failed to read pH value");
      return;
    }

    // Parse pH value
    float ph_value;
    if (!this->parse_ph_value_(response, ph_value)) {
      ESP_LOGV(TAG, "Continuous mode: Failed to parse response: %s", response);
      return;
    }

    // Validate pH range - (Commented out for testing continuous mode behavior)
    // if (ph_value < PH_MIN || ph_value > PH_MAX || std::isnan(ph_value)) {
    //   ESP_LOGW(TAG, "Continuous mode: pH value out of valid range: %.2f", ph_value);
    //   return;
    // }

    // Success - update and publish
    this->error_count_ = 0;
    this->update_stability_buffer_(ph_value);

    if (this->ph_sensor_ != nullptr) {
      this->ph_sensor_->publish_state(ph_value);
    }

    // Log each reading prominently in continuous mode
    ESP_LOGI(TAG, "📊 Continuous pH Reading: %.2f", ph_value);
    return;
  }

  // ========================================
  // Normal Polling Mode (below)
  // ========================================

  // Step 1: Send temperature compensation if configured
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

  // Step 2: Request single pH reading
  if (!this->send_command_("R")) {
    ESP_LOGW(TAG, "Failed to send read command");
    this->error_count_++;
    if (this->error_count_ > MAX_ERRORS) {
      ESP_LOGE(TAG, "Too many consecutive errors, marking sensor as not ready");
      this->sensor_ready_ = false;
    }
    return;
  }

  // Wait for sensor to acquire and process pH measurement
  // Critical timing: EZO circuit needs 300ms to complete measurement
  this->wait_for_response_();

  // Step 3: Read and parse pH response
  char response[RESPONSE_BUFFER_SIZE];
  if (!this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    ESP_LOGW(TAG, "Failed to read pH value from UART");
    this->error_count_++;
    if (this->error_count_ > MAX_ERRORS) {
      ESP_LOGE(TAG, "Too many consecutive errors, marking sensor as not ready");
      this->sensor_ready_ = false;
    }
    return;
  }

  // Parse pH value from ASCII response
  float ph_value;
  if (!this->parse_ph_value_(response, ph_value)) {
    ESP_LOGW(TAG, "Failed to parse pH value from response: %s", response);
    this->error_count_++;
    if (this->error_count_ > MAX_ERRORS) {
      ESP_LOGE(TAG, "Too many consecutive errors, marking sensor as not ready");
      this->sensor_ready_ = false;
    }
    return;
  }

  // Step 4: Validate pH range
  if (ph_value < PH_MIN || ph_value > PH_MAX || std::isnan(ph_value)) {
    ESP_LOGW(TAG, "pH value out of valid range: %.2f", ph_value);
    this->error_count_++;
    if (this->error_count_ > MAX_ERRORS) {
      ESP_LOGE(TAG, "Too many consecutive errors, marking sensor as not ready");
      this->sensor_ready_ = false;
    }
    return;
  }

  // Step 5: Success - reset error counter and publish
  this->error_count_ = 0;

  // Update stability tracking buffer
  this->update_stability_buffer_(ph_value);

  // Publish pH value to ESPHome sensor
  if (this->ph_sensor_ != nullptr) {
    this->ph_sensor_->publish_state(ph_value);
    ESP_LOGD(TAG, "pH: %.2f", ph_value);
  }
}

void EZOPHUARTComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "EZO pH Sensor (UART):");
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "pH", this->ph_sensor_);

  // Log temperature compensation status
  if (this->temp_sensor_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Temperature Compensation: Enabled");
  } else {
    ESP_LOGCONFIG(TAG, "  Temperature Compensation: Disabled");
  }

  // Log sensor readiness
  if (!this->sensor_ready_) {
    ESP_LOGE(TAG, "  Sensor Status: NOT READY (check UART connection)");
  }
}

// ============================================================================
// Calibration Methods
// ============================================================================

bool EZOPHUARTComponent::calibrate_mid(float ph_value) {
  char cmd[20];
  snprintf(cmd, sizeof(cmd), "Cal,mid,%.2f", ph_value);

  ESP_LOGI(TAG, "Starting mid-point calibration at pH %.2f", ph_value);

  if (!this->send_command_(cmd)) {
    ESP_LOGE(TAG, "Failed to send mid-point calibration command");
    return false;
  }

  this->wait_for_response_();

  char response[RESPONSE_BUFFER_SIZE];
  if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    if (this->check_response_code_(response)) {
      ESP_LOGI(TAG, "Mid-point calibration successful at pH %.2f", ph_value);
      return true;
    } else {
      ESP_LOGE(TAG, "Mid-point calibration failed: %s", response);
      return false;
    }
  }

  ESP_LOGE(TAG, "No response from sensor during mid-point calibration");
  return false;
}

bool EZOPHUARTComponent::calibrate_low(float ph_value) {
  char cmd[20];
  snprintf(cmd, sizeof(cmd), "Cal,low,%.2f", ph_value);

  ESP_LOGI(TAG, "Starting low-point calibration at pH %.2f", ph_value);

  if (!this->send_command_(cmd)) {
    ESP_LOGE(TAG, "Failed to send low-point calibration command");
    return false;
  }

  this->wait_for_response_();

  char response[RESPONSE_BUFFER_SIZE];
  if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    if (this->check_response_code_(response)) {
      ESP_LOGI(TAG, "Low-point calibration successful at pH %.2f", ph_value);
      return true;
    } else {
      ESP_LOGE(TAG, "Low-point calibration failed: %s", response);
      return false;
    }
  }

  ESP_LOGE(TAG, "No response from sensor during low-point calibration");
  return false;
}

bool EZOPHUARTComponent::calibrate_high(float ph_value) {
  char cmd[20];
  snprintf(cmd, sizeof(cmd), "Cal,high,%.2f", ph_value);

  ESP_LOGI(TAG, "Starting high-point calibration at pH %.2f", ph_value);

  if (!this->send_command_(cmd)) {
    ESP_LOGE(TAG, "Failed to send high-point calibration command");
    return false;
  }

  this->wait_for_response_();

  char response[RESPONSE_BUFFER_SIZE];
  if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    if (this->check_response_code_(response)) {
      ESP_LOGI(TAG, "High-point calibration successful at pH %.2f", ph_value);
      return true;
    } else {
      ESP_LOGE(TAG, "High-point calibration failed: %s", response);
      return false;
    }
  }

  ESP_LOGE(TAG, "No response from sensor during high-point calibration");
  return false;
}

void EZOPHUARTComponent::calibrate_clear() {
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

void EZOPHUARTComponent::query_calibration_status() {
  ESP_LOGI(TAG, "Querying calibration status");

  if (!this->send_command_("Cal,?")) {
    ESP_LOGE(TAG, "Failed to send calibration query command");
    return;
  }

  this->wait_for_response_();

  char response[RESPONSE_BUFFER_SIZE];
  if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    ESP_LOGI(TAG, "Calibration status: %s", response);
    // Response format: ?Cal,n where n is 0, 1, 2, or 3 (number of calibration points)
  }
}

// ============================================================================
// Advanced Features
// ============================================================================

void EZOPHUARTComponent::set_led(bool enable) {
  const char *cmd = enable ? "L,1" : "L,0";
  ESP_LOGD(TAG, "Setting EZO LED: %s", enable ? "ON" : "OFF");

  if (this->send_command_(cmd)) {
    this->wait_for_response_();
    char response[RESPONSE_BUFFER_SIZE];
    if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
      this->check_response_code_(response);
    }
  }
}

void EZOPHUARTComponent::enter_sleep_mode() {
  ESP_LOGI(TAG, "Entering low-power sleep mode");

  if (this->send_command_("Sleep")) {
    // Note: No response expected after sleep command
    // Sensor will wake on next UART activity or power cycle
    this->sensor_ready_ = false;
  }
}

bool EZOPHUARTComponent::is_stable() {
  // Need full buffer before we can determine stability
  if (this->stability_count_ < STABILITY_BUFFER_SIZE) {
    return false;
  }

  // Calculate standard deviation of recent readings
  float std_dev = this->calculate_std_deviation_();

  // Stable if standard deviation is below threshold
  return std_dev < STABILITY_THRESHOLD;
}

bool EZOPHUARTComponent::take_single_reading(float &value) {
  if (!this->sensor_ready_) {
    ESP_LOGW(TAG, "Cannot take reading - sensor not ready");
    return false;
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
    return false;
  }

  // Wait for sensor to process measurement
  this->wait_for_response_();

  // Read and parse response
  char response[RESPONSE_BUFFER_SIZE];
  if (!this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
    ESP_LOGW(TAG, "Failed to read pH value from UART");
    return false;
  }

  // Parse pH value
  if (!this->parse_ph_value_(response, value)) {
    ESP_LOGW(TAG, "Failed to parse pH value from response: %s", response);
    return false;
  }

  // Validate range
  if (value < PH_MIN || value > PH_MAX || std::isnan(value)) {
    ESP_LOGW(TAG, "pH value out of valid range: %.2f", value);
    return false;
  }

  // Update stability buffer
  this->update_stability_buffer_(value);

  ESP_LOGD(TAG, "Single pH reading: %.2f", value);
  return true;
}

float EZOPHUARTComponent::get_last_reading() const {
  if (this->stability_count_ == 0) {
    return 0.0f;
  }

  // Get most recent reading (previous index in circular buffer)
  size_t last_index = (this->stability_index_ == 0)
                      ? (STABILITY_BUFFER_SIZE - 1)
                      : (this->stability_index_ - 1);

  return this->stability_buffer_[last_index];
}

float EZOPHUARTComponent::get_average_reading(size_t count) const {
  if (this->stability_count_ == 0) {
    return 0.0f;
  }

  // Limit count to available readings
  size_t actual_count = std::min(count, this->stability_count_);
  if (actual_count > STABILITY_BUFFER_SIZE) {
    actual_count = STABILITY_BUFFER_SIZE;
  }

  // Calculate average of last N readings
  float sum = 0.0f;
  for (size_t i = 0; i < actual_count; i++) {
    // Walk backwards from current index
    size_t idx = (this->stability_index_ + STABILITY_BUFFER_SIZE - 1 - i) % STABILITY_BUFFER_SIZE;
    sum += this->stability_buffer_[idx];
  }

  return sum / actual_count;
}

void EZOPHUARTComponent::enable_continuous_reading() {
  ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");
  ESP_LOGI(TAG, "  ENABLING CONTINUOUS pH READING MODE");
  ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");

  // Flush any pending data before mode change
  this->flush();
  delay(50);

  if (this->send_command_("C,1")) {
    this->wait_for_response_();
    char response[RESPONSE_BUFFER_SIZE];
    if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
      if (this->check_response_code_(response)) {
        this->continuous_mode_active_ = true;
        ESP_LOGI(TAG, "✓ Continuous reading mode ENABLED");
        ESP_LOGI(TAG, "  Sensor will automatically output pH every 1 second");
        ESP_LOGI(TAG, "  Each reading will be logged with: 📊 Continuous pH Reading");
        ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");
      } else {
        ESP_LOGE(TAG, "✗ Failed to enable continuous reading: %s", response);
        ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");
      }
    }
  } else {
    ESP_LOGE(TAG, "✗ Failed to send continuous reading enable command");
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");
  }

  // Flush any remaining data after mode change
  this->flush();
  delay(50);
}

void EZOPHUARTComponent::disable_continuous_reading() {
  ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");
  ESP_LOGI(TAG, "  DISABLING CONTINUOUS pH READING MODE");
  ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");

  // Flush any pending data before mode change
  this->flush();
  delay(50);

  if (this->send_command_("C,0")) {
    this->wait_for_response_();
    char response[RESPONSE_BUFFER_SIZE];
    if (this->read_response_(response, RESPONSE_BUFFER_SIZE)) {
      if (this->check_response_code_(response)) {
        this->continuous_mode_active_ = false;
        ESP_LOGI(TAG, "✓ Continuous reading mode DISABLED");
        ESP_LOGI(TAG, "  Sensor returned to command mode (polling only)");
        ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");
      } else {
        ESP_LOGE(TAG, "✗ Failed to disable continuous reading: %s", response);
        ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");
      }
    }
  } else {
    ESP_LOGE(TAG, "✗ Failed to send continuous reading disable command");
    ESP_LOGI(TAG, "═══════════════════════════════════════════════════════");
  }

  // Flush any remaining data after mode change
  this->flush();
  delay(50);
}

// ============================================================================
// UART Communication Helpers
// ============================================================================

bool EZOPHUARTComponent::send_command_(const char *cmd) {
  // EZO circuits expect commands terminated with carriage return
  char cmd_with_cr[RESPONSE_BUFFER_SIZE];
  snprintf(cmd_with_cr, sizeof(cmd_with_cr), "%s\r", cmd);

  // Log command based on verbose mode
  if (this->verbose_) {
    ESP_LOGI(TAG, "→ TX: %s", cmd);
  } else {
    ESP_LOGV(TAG, "Sending UART command: %s", cmd);
  }

  // Write command to UART
  this->write_array(reinterpret_cast<const uint8_t *>(cmd_with_cr), strlen(cmd_with_cr));
  this->flush();  // Ensure data is transmitted

  return true;
}

bool EZOPHUARTComponent::read_response_(char *buffer, size_t len) {
  // Clear buffer before reading
  memset(buffer, 0, len);

  // Read bytes until carriage return, newline, or buffer full
  size_t pos = 0;
  uint32_t start_time = millis();
  const uint32_t timeout_ms = 1000;  // 1 second timeout

  while (pos < len - 1 && (millis() - start_time) < timeout_ms) {
    if (this->available()) {
      uint8_t byte;
      this->read_byte(&byte);

      // Check for end of response (carriage return or newline)
      if (byte == '\r' || byte == '\n') {
        // If we have data, we're done
        if (pos > 0) {
          break;
        }
        // Otherwise, skip leading CR/LF and continue
        continue;
      }

      // Add byte to buffer
      buffer[pos++] = byte;
    } else {
      // No data available, yield to other tasks briefly
      delay(1);
    }
  }

  // Null-terminate the string
  buffer[pos] = '\0';

  // Strip any trailing whitespace
  while (pos > 0 && (buffer[pos - 1] == ' ' || buffer[pos - 1] == '\t')) {
    buffer[--pos] = '\0';
  }

  // Check if response is just an echo of the command (single character only)
  // Real responses should have commas and values (e.g., "C,1" or "7.45")
  if (pos == 1 && (buffer[0] == 'R' || buffer[0] == 'C' || buffer[0] == 'T' || buffer[0] == 'i')) {
    ESP_LOGW(TAG, "Detected command echo: '%s' - reading actual response...", buffer);

    // Clear buffer and read again for the actual response
    memset(buffer, 0, len);
    pos = 0;
    start_time = millis();

    // Read the actual response after the echo
    while (pos < len - 1 && (millis() - start_time) < timeout_ms) {
      if (this->available()) {
        uint8_t byte;
        this->read_byte(&byte);

        // Check for end of response
        if (byte == '\r' || byte == '\n') {
          if (pos > 0) {
            break;
          }
          continue;
        }

        buffer[pos++] = byte;
      } else {
        delay(1);
      }
    }

    // Null-terminate
    buffer[pos] = '\0';

    // Strip trailing whitespace
    while (pos > 0 && (buffer[pos - 1] == ' ' || buffer[pos - 1] == '\t')) {
      buffer[--pos] = '\0';
    }

    if (this->verbose_) {
      ESP_LOGI(TAG, "← RX (after echo): %s", buffer);
    } else {
      ESP_LOGD(TAG, "Read actual response after echo: %s (length: %d)", buffer, pos);
    }
  }

  // Log response based on verbose mode
  if (this->verbose_) {
    ESP_LOGI(TAG, "← RX: %s", buffer);
  } else {
    ESP_LOGV(TAG, "Received UART response: %s (length: %d)", buffer, pos);
  }

  return pos > 0;
}

bool EZOPHUARTComponent::parse_ph_value_(const char *response, float &value) {
  if (response == nullptr || strlen(response) == 0) {
    return false;
  }

  // Reject command confirmation responses (these are not pH readings)
  // C,0 or C,1 = continuous mode status
  // T,xx.x = temperature compensation confirmation
  // Cal,... = calibration responses
  if (response[0] == 'C' && response[1] == ',') {
    ESP_LOGD(TAG, "Ignoring continuous mode status response: %s", response);
    return false;
  }
  if (response[0] == 'T' && response[1] == ',') {
    ESP_LOGD(TAG, "Ignoring temperature compensation confirmation: %s", response);
    return false;
  }
  if (strncmp(response, "Cal,", 4) == 0 || strncmp(response, "?Cal,", 5) == 0) {
    ESP_LOGD(TAG, "Ignoring calibration response: %s", response);
    return false;
  }

  // Handle both verbose and non-verbose response modes
  // Verbose mode: "*OK,6.54" or "*ER"
  // Non-verbose mode: "6.54"
  const char *value_start = response;

  // Check for verbose response codes (RESPONSE,1 mode)
  if (response[0] == '*') {
    if (strncmp(response, "*OK", 3) == 0) {
      // Find comma after *OK
      value_start = strchr(response, ',');
      if (value_start != nullptr) {
        value_start++;  // Skip the comma
      } else {
        // No value after *OK (just confirmation, not a reading)
        ESP_LOGD(TAG, "Response is just confirmation with no pH value: %s", response);
        return false;
      }
    } else if (strncmp(response, "*ER", 3) == 0 || strncmp(response, "*OV", 3) == 0 ||
               strncmp(response, "*UV", 3) == 0) {
      // Error response - log and reject
      ESP_LOGW(TAG, "EZO error response: %s", response);
      return false;
    }
  }
  // In non-verbose mode (RESPONSE,0), response starts directly with the value

  // Parse floating-point value from ASCII
  char *endptr;
  value = strtof(value_start, &endptr);

  // Verify parsing was successful
  if (endptr == value_start || std::isnan(value)) {
    ESP_LOGW(TAG, "Failed to parse float from: %s", value_start);
    return false;
  }

  return true;
}

bool EZOPHUARTComponent::check_response_code_(const char *response) {
  if (response == nullptr || strlen(response) == 0) {
    return false;
  }

  // In non-verbose mode (RESPONSE,0), successful commands return just the data
  // In verbose mode (RESPONSE,1), successful commands return "*OK" or "*OK,data"

  // Check for success response (verbose mode)
  if (strncmp(response, "*OK", 3) == 0) {
    return true;
  }

  // Check for error responses and log appropriately
  if (strncmp(response, "*ER", 3) == 0) {
    ESP_LOGW(TAG, "EZO command error: %s", response);
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
    ESP_LOGW(TAG, "EZO device reset detected: %s", response);
    return false;
  }

  // In non-verbose mode, any data response without error codes is considered success
  // This includes: "6.54", "C,0", "?I,pH,1.0", etc.
  return true;
}

void EZOPHUARTComponent::wait_for_response_() {
  // Critical timing requirement for EZO circuits:
  // Must wait 300ms after sending command before reading response
  // This allows the sensor to process the command and prepare data
  delay(RESPONSE_DELAY_MS);
}

// ============================================================================
// Stability Tracking Helpers
// ============================================================================

void EZOPHUARTComponent::update_stability_buffer_(float value) {
  // Store value in circular buffer
  this->stability_buffer_[this->stability_index_] = value;

  // Advance index with wraparound
  this->stability_index_ = (this->stability_index_ + 1) % STABILITY_BUFFER_SIZE;

  // Track how many valid readings we have (up to buffer size)
  if (this->stability_count_ < STABILITY_BUFFER_SIZE) {
    this->stability_count_++;
  }
}

float EZOPHUARTComponent::calculate_std_deviation_() {
  if (this->stability_count_ == 0) {
    return 0.0f;
  }

  // Calculate mean (average) of all readings
  float sum = 0.0f;
  for (size_t i = 0; i < this->stability_count_; i++) {
    sum += this->stability_buffer_[i];
  }
  float mean = sum / this->stability_count_;

  // Calculate variance (average squared deviation from mean)
  float variance_sum = 0.0f;
  for (size_t i = 0; i < this->stability_count_; i++) {
    float diff = this->stability_buffer_[i] - mean;
    variance_sum += diff * diff;
  }
  float variance = variance_sum / this->stability_count_;

  // Return standard deviation (square root of variance)
  return sqrt(variance);
}

}  // namespace ezo_ph_uart
}  // namespace esphome
