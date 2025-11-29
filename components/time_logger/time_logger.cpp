#include "time_logger.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cstdio>

namespace esphome {
namespace time_logger {

static const char *TAG = "time_logger";

void TimeLogger::setup() {
  /**
   * Initialize the time logger component.
   *
   * Set last_log_time_ to current millis() to establish the baseline for
   * the first interval calculation. Without this, the first log would occur
   * immediately instead of after log_interval_ milliseconds.
   *
   * WHY millis() (not 0):
   * - If we set last_log_time_ = 0, then elapsed = millis() - 0
   * - On a system that has been running for hours, this would be huge
   * - The first check would immediately trigger (since elapsed >> log_interval_)
   * - Starting with current millis() ensures first interval is accurate
   */
  this->last_log_time_ = millis();

  ESP_LOGI(TAG, "Time logger initialized (interval: %u ms)", this->log_interval_);
}

void TimeLogger::loop() {
  /**
   * Non-blocking periodic execution.
   *
   * This function is called continuously by ESPHome's main event loop
   * (~1000 times per second). We check if enough time has elapsed since
   * the last log, and if so, perform the logging action.
   *
   * TIMING CALCULATION:
   * - current_time: Current timestamp from millis()
   * - elapsed: Time since last log (current_time - last_log_time_)
   * - If elapsed >= log_interval_, it's time to log again
   *
   * WRAPAROUND SAFETY:
   * millis() overflows after ~49.7 days (2^32 milliseconds). However,
   * unsigned integer subtraction handles wraparound correctly:
   *
   * Example near overflow:
   * - last_log_time_ = 4294967290 (near max uint32_t)
   * - millis() wraps to 100
   * - elapsed = 100 - 4294967290 = 810 (correct!)
   *
   * This works because unsigned subtraction uses modulo 2^32 arithmetic.
   */
  uint32_t current_time = millis();
  uint32_t elapsed = current_time - this->last_log_time_;

  // Check if interval has elapsed
  if (elapsed >= this->log_interval_) {
    // Perform the logging action
    log_current_time();

    // Update last log time for next interval
    // WHY current_time (not millis()):
    // - log_current_time() might take a few milliseconds
    // - Using current_time ensures consistent interval timing
    // - Prevents drift accumulation over time
    //
    // ALTERNATIVE (less accurate):
    // this->last_log_time_ += this->log_interval_;
    // This can accumulate drift if logging takes variable time
    this->last_log_time_ = current_time;
  }

  /**
   * IMPORTANT: This function must return quickly!
   *
   * loop() is called ~1000 times per second. If we block here (e.g., with
   * delay()), we freeze the entire ESP32 event loop, preventing:
   * - WiFi connectivity
   * - OTA updates
   * - Web server
   * - Other components
   *
   * The non-blocking pattern (checking elapsed time) allows loop() to
   * return immediately most of the time, keeping the system responsive.
   */
}

void TimeLogger::log_current_time() {
  /**
   * Query the time component and log formatted date/time.
   *
   * This function:
   * 1. Checks that time_source_ is configured (nullptr safety)
   * 2. Retrieves current time from the NTP-synchronized time component
   * 3. Validates that time has been synchronized
   * 4. Formats the time as dd.mm.yyyy HH:MM:SS
   * 5. Logs to serial monitor
   *
   * ERROR HANDLING:
   * - If time_source_ is nullptr: Component not configured in YAML
   * - If !current_time.is_valid(): NTP not yet synchronized
   * Both cases are logged as warnings and gracefully handled.
   */

  // Safety check: Ensure time component is configured
  if (this->time_source_ == nullptr) {
    ESP_LOGW(TAG, "Time source not configured - cannot log time");
    return;
  }

  // Query current time from the time component
  // now() returns an ESPTime struct with date/time fields
  auto current_time = this->time_source_->now();

  // Validate that time has been synchronized with NTP
  // is_valid() returns false until first successful NTP sync
  if (!current_time.is_valid()) {
    ESP_LOGW(TAG, "Time not synchronized yet - waiting for NTP");
    return;
  }

  /**
   * Format the time string: dd.mm.yyyy HH:MM:SS
   *
   * ESPTime struct fields:
   * - year: 4-digit year (e.g., 2025)
   * - month: 1-12
   * - day_of_month: 1-31
   * - hour: 0-23 (24-hour format)
   * - minute: 0-59
   * - second: 0-59
   *
   * WHY snprintf (not std::string or sprintf):
   * - snprintf is size-safe (prevents buffer overflow)
   * - Lightweight (no heap allocation like std::string)
   * - Standard C library function (available on ESP32)
   * - More efficient than string concatenation
   *
   * FORMAT STRING: %02d.%02d.%04d %02d:%02d:%02d
   * - %02d: 2-digit decimal with leading zero (e.g., 05, 12)
   * - %04d: 4-digit decimal with leading zeros (e.g., 2025)
   * - Produces: 29.11.2025 14:35:22
   *
   * BUFFER SIZE: 32 bytes
   * - "dd.mm.yyyy HH:MM:SS" = 19 characters
   * - +1 for null terminator = 20 bytes minimum
   * - 32 bytes provides safety margin
   */
  char time_str[32];
  snprintf(time_str, sizeof(time_str),
           "%02d.%02d.%04d %02d:%02d:%02d",
           current_time.day_of_month,
           current_time.month,
           current_time.year,
           current_time.hour,
           current_time.minute,
           current_time.second);

  /**
   * Log the formatted time to serial monitor.
   *
   * ESP_LOGI: Info-level log (displayed at INFO, DEBUG, VERBOSE levels)
   * - TAG: "time_logger" (identifies log source)
   * - Message: "Current time: dd.mm.yyyy HH:MM:SS"
   *
   * LOG LEVELS:
   * - ERROR: Critical failures
   * - WARN: Potential issues (NTP not synced)
   * - INFO: Normal operational messages (our time logs)
   * - DEBUG: Detailed debugging
   * - VERBOSE: Very detailed debugging
   *
   * This will appear in the serial monitor as:
   * [I][time_logger:XXX]: Current time: 29.11.2025 14:35:22
   */
  ESP_LOGI(TAG, "Current time: %s", time_str);
}

} // namespace time_logger
} // namespace esphome
