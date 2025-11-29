#pragma once
#include "esphome/core/component.h"
#include "esphome/components/wifi/wifi_component.h"

namespace esphome {
namespace ip_logger {

/**
 * IPLogger: Periodic IP Address Logging Component
 *
 * ============================================================================
 * COMPONENT OVERVIEW
 * ============================================================================
 *
 * This component demonstrates non-blocking periodic execution in ESPHome
 * while accessing WiFi status and IP address information. It logs the
 * ESP32's current local IP address to the serial monitor at a configurable
 * interval (default: 30 seconds).
 *
 * ============================================================================
 * NON-BLOCKING TIMING PATTERN
 * ============================================================================
 *
 * WHY NOT delay():
 * - delay() blocks the entire ESP32 event loop
 * - WiFi, OTA updates, web server, other components would freeze
 * - ESPHome requires components to be responsive and non-blocking
 *
 * INSTEAD, we use millis() for non-blocking timing:
 * 1. Store last_log_time_ timestamp when we perform an action
 * 2. In loop(), calculate elapsed = millis() - last_log_time_
 * 3. If elapsed >= log_interval_, perform action and update last_log_time_
 * 4. Return immediately, allowing other components to run
 *
 * This pattern keeps the system responsive while achieving precise timing.
 *
 * ============================================================================
 * WIFI INTEGRATION
 * ============================================================================
 *
 * The IPLogger accesses the global WiFi component (wifi::global_wifi_component)
 * to retrieve:
 * - WiFi connection status (is_connected())
 * - Current local IP address (get_ip_address())
 *
 * The component only logs when WiFi is connected, preventing misleading
 * output like "Current IP: 0.0.0.0" during disconnection or startup.
 *
 * ============================================================================
 * IP ADDRESS FORMATTING
 * ============================================================================
 *
 * IP addresses are retrieved as network::IPAddress objects and converted
 * to human-readable strings (e.g., "192.168.1.100").
 *
 * ESPHome's network::IPAddress provides a str() method that formats the
 * address as a standard dotted-decimal string.
 *
 * Output format: "Current IP: 192.168.1.100"
 */
class IPLogger : public Component {
 public:
  /**
   * setup() - Initialize the component
   *
   * Called once at boot. Initializes timing variables and logs that the
   * IP logger is ready.
   */
  void setup() override;

  /**
   * loop() - Non-blocking periodic execution
   *
   * Called continuously by ESPHome's main event loop (~1000 Hz).
   * Checks if log_interval_ has elapsed, and if so, logs the current IP.
   *
   * This method must return quickly to avoid blocking other components.
   */
  void loop() override;

  /**
   * set_log_interval() - Set the logging interval in milliseconds
   *
   * @param interval Milliseconds between log outputs (default: 30000)
   *
   * Configures how often to log the IP address. Common values:
   * - 10000ms (10s): More frequent logging for debugging
   * - 30000ms (30s): Default, good balance
   * - 60000ms (1min): Less frequent for production
   */
  void set_log_interval(uint32_t interval) {
    log_interval_ = interval;
  }

 private:
  // ===== Timing Configuration =====

  /**
   * Interval between log outputs in milliseconds.
   *
   * Default: 30000ms (30 seconds)
   * Configured via YAML or defaults to 30s if not specified.
   */
  uint32_t log_interval_{30000};

  /**
   * Timestamp (millis()) of the last log output.
   *
   * WHY uint32_t:
   * - millis() returns uint32_t
   * - Overflows after ~49.7 days, but subtraction still works correctly
   *   due to unsigned integer wraparound behavior
   *
   * Example: If millis() overflows:
   * - last_log_time_ = 4294967290 (near max uint32_t)
   * - millis() = 100 (after overflow)
   * - elapsed = 100 - 4294967290 = 810 (correct due to wraparound!)
   *
   * This makes millis()-based timing robust to overflow.
   */
  uint32_t last_log_time_{0};

  // ===== Helper Functions =====

  /**
   * log_current_ip() - Retrieve WiFi status and log IP address
   *
   * Checks WiFi connection status and logs the current IP address if
   * connected. If WiFi is not connected, logs a warning instead.
   *
   * Output format:
   * - Connected: "Current IP: 192.168.1.100"
   * - Not connected: "WiFi not connected - cannot log IP address"
   */
  void log_current_ip();
};

} // namespace ip_logger
} // namespace esphome
