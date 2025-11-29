#include "ip_logger.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace ip_logger {

static const char *TAG = "ip_logger";

void IPLogger::setup() {
  /**
   * Initialize the IP logger component.
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

  ESP_LOGI(TAG, "IP logger initialized (interval: %u ms)", this->log_interval_);
}

void IPLogger::loop() {
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
    log_current_ip();

    // Update last log time for next interval
    // WHY current_time (not millis()):
    // - log_current_ip() might take a few milliseconds
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

void IPLogger::log_current_ip() {
  /**
   * Retrieve WiFi status and log the current IP address.
   *
   * This function:
   * 1. Accesses the global WiFi component
   * 2. Checks if WiFi is connected
   * 3. Retrieves the current local IP address
   * 4. Formats and logs the IP address
   *
   * WIFI CONNECTION STATES:
   * - Connected: WiFi has established connection to access point
   * - Connecting: WiFi is attempting to connect (no IP yet)
   * - Disconnected: WiFi is not connected
   *
   * We only log the IP when in "Connected" state to avoid logging
   * invalid addresses like "0.0.0.0".
   *
   * ERROR HANDLING:
   * - If WiFi component is nullptr: Component not configured
   * - If !is_connected(): WiFi not connected yet
   * Both cases are logged as debug/warning messages.
   */

  // Access the global WiFi component
  // wifi::global_wifi_component is a singleton instance created by ESPHome
  // when wifi: is configured in YAML
  auto wifi = wifi::global_wifi_component;

  // Safety check: Ensure WiFi component exists
  if (wifi == nullptr) {
    ESP_LOGW(TAG, "WiFi component not available - cannot log IP address");
    return;
  }

  // Check if WiFi is connected
  // is_connected() returns true only when fully connected to an access point
  // It returns false during:
  // - Initial boot (before first connection)
  // - Connection attempts (waiting for DHCP)
  // - Disconnection events (network issues, AP out of range)
  if (!wifi->is_connected()) {
    ESP_LOGD(TAG, "WiFi not connected - skipping IP log");
    return;
  }

  /**
   * Retrieve the current IP address.
   *
   * get_ip_addresses() returns a list of network::IPAddress objects.
   * For most configurations, the first address [0] is the IPv4 address
   * assigned by DHCP (or configured statically).
   *
   * WHY CHECK is_connected() FIRST:
   * - get_ip_addresses() returns valid data only when connected
   * - Without the check, we might get 0.0.0.0 (invalid address)
   * - Checking is_connected() ensures we have a valid IP
   */
  auto ip_addresses = wifi->get_ip_addresses();

  // Check if we have at least one IP address
  if (ip_addresses.empty()) {
    ESP_LOGD(TAG, "No IP addresses available yet");
    return;
  }

  // Use the first IP address (typically the IPv4 address)
  network::IPAddress ip = ip_addresses[0];

  /**
   * Format and log the IP address.
   *
   * network::IPAddress provides a str() method that converts the
   * IP address to a human-readable string in dotted-decimal notation.
   *
   * Examples:
   * - 192.168.1.100
   * - 10.0.0.45
   * - 172.16.50.200
   *
   * .c_str() converts the std::string to a C-style string for printf-style
   * logging functions.
   *
   * LOG FORMAT:
   * "Current IP: 192.168.1.100"
   *
   * This will appear in the serial monitor as:
   * [I][ip_logger:XXX]: Current IP: 192.168.1.100
   */
  ESP_LOGI(TAG, "Current IP: %s", ip.str().c_str());
}

} // namespace ip_logger
} // namespace esphome
