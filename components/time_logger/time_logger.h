#pragma once
#include "esphome/core/component.h"
#include "esphome/components/time/real_time_clock.h"

namespace esphome {
namespace time_logger {

/**
 * TimeLogger: Periodic Time Logging Component
 *
 * ============================================================================
 * COMPONENT OVERVIEW
 * ============================================================================
 *
 * This component demonstrates non-blocking periodic execution in ESPHome.
 * It queries an NTP-synchronized time component and logs the current date
 * and time to the serial monitor at a configurable interval (default: 5s).
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
 * TIME FORMATTING
 * ============================================================================
 *
 * ESPHome's time component provides a now() method that returns an ESPTime
 * struct containing:
 * - year, month, day_of_month (date components)
 * - hour, minute, second (time components)
 * - is_valid() (whether time has been synchronized)
 *
 * We format this as: dd.mm.yyyy HH:MM:SS
 * Example: 29.11.2025 14:35:22
 *
 * ============================================================================
 * INTEGRATION WITH NTP
 * ============================================================================
 *
 * The TimeLogger receives a reference to a time::RealTimeClock component
 * (typically configured as SNTP/NTP in YAML). This component handles:
 * - NTP synchronization with time servers
 * - Timezone conversion (CET/CEST in our case)
 * - Daylight saving time adjustments
 *
 * The logger simply queries the current time and doesn't need to know about
 * NTP details - that's the responsibility of the time component.
 */
class TimeLogger : public Component {
 public:
  /**
   * setup() - Initialize the component
   *
   * Called once at boot. Initializes timing variables and logs that the
   * time logger is ready.
   */
  void setup() override;

  /**
   * loop() - Non-blocking periodic execution
   *
   * Called continuously by ESPHome's main event loop (~1000 Hz).
   * Checks if log_interval_ has elapsed, and if so, logs the current time.
   *
   * This method must return quickly to avoid blocking other components.
   */
  void loop() override;

  /**
   * Dependency injection setters (called by generated code from Python)
   */

  /**
   * set_time_source() - Inject the time component reference
   *
   * @param time_source Pointer to a time::RealTimeClock component
   *
   * This allows the logger to query the current time. The time component
   * handles NTP synchronization and timezone conversion.
   */
  void set_time_source(time::RealTimeClock *time_source) {
    time_source_ = time_source;
  }

  /**
   * set_log_interval() - Set the logging interval in milliseconds
   *
   * @param interval Milliseconds between log outputs (default: 5000)
   *
   * Configures how often to log the time. Common values:
   * - 1000ms (1s): High-frequency logging for debugging
   * - 5000ms (5s): Default, good balance between visibility and log spam
   * - 60000ms (1min): Low-frequency for production monitoring
   */
  void set_log_interval(uint32_t interval) {
    log_interval_ = interval;
  }

 private:
  // ===== Component Dependencies (injected via setters) =====

  /**
   * Pointer to time component providing synchronized time.
   *
   * WHY POINTER:
   * - Matches ESPHome's component linking pattern
   * - Can be nullptr, allowing nullptr checks for safety
   * - Set via dependency injection in to_code()
   */
  time::RealTimeClock *time_source_{nullptr};

  // ===== Timing Configuration =====

  /**
   * Interval between log outputs in milliseconds.
   *
   * Default: 5000ms (5 seconds)
   * Configured via YAML or defaults to 5s if not specified.
   */
  uint32_t log_interval_{5000};

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
   * log_current_time() - Query time component and log formatted time
   *
   * Retrieves current time from time_source_, formats it as:
   * dd.mm.yyyy HH:MM:SS
   *
   * And logs to serial monitor using ESP_LOGI.
   *
   * Handles cases where:
   * - time_source_ is nullptr (component not configured)
   * - Time is not yet synchronized (NTP not completed)
   */
  void log_current_time();
};

} // namespace time_logger
} // namespace esphome
