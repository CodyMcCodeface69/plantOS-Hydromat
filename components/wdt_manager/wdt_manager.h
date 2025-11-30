#pragma once
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

#ifdef USE_ESP_IDF
#include "esp_task_wdt.h"
#endif

namespace esphome {
namespace wdt_manager {

/**
 * WDTManager: Hardware Watchdog Timer Management Component
 *
 * ============================================================================
 * COMPONENT OVERVIEW
 * ============================================================================
 *
 * This component manages the ESP32's hardware watchdog timer (WDT) to detect
 * and recover from system hangs or crashes. The WDT must be regularly "fed"
 * to indicate the system is healthy. If feeding stops (due to a crash or
 * infinite loop), the WDT automatically resets the device.
 *
 * ============================================================================
 * WATCHDOG TIMER OPERATION
 * ============================================================================
 *
 * 1. Initialization (setup):
 *    - Configure hardware WDT with timeout period (default: 10 seconds)
 *    - Subscribe current task to WDT monitoring
 *    - Log initialization status
 *
 * 2. Normal Operation (loop):
 *    - Feed WDT every feed_interval (default: 500ms)
 *    - Log feeding activity for monitoring
 *    - Use non-blocking millis() timing
 *
 * 3. Crash Detection:
 *    - If WDT is not fed within timeout period, hardware triggers reset
 *    - System reboots automatically
 *    - No software intervention possible (hardware-enforced safety)
 *
 * 4. Test Mode (optional):
 *    - After crash_delay (default: 20s), stop feeding WDT
 *    - Simulates system hang for testing
 *    - WDT timeout triggers automatic reboot
 *
 * ============================================================================
 * ESP-IDF TASK WATCHDOG TIMER (TWDT)
 * ============================================================================
 *
 * ESP32 uses a timer-based watchdog (TWDT) that monitors tasks:
 * - esp_task_wdt_init(): Initialize WDT with timeout
 * - esp_task_wdt_add(): Subscribe current task to monitoring
 * - esp_task_wdt_reset(): Feed/reset the watchdog timer
 * - esp_task_wdt_delete(): Unsubscribe from monitoring (cleanup)
 *
 * IMPORTANT: The WDT operates at the hardware level and will reset the
 * device even if software is completely frozen.
 *
 * ============================================================================
 * NON-BLOCKING TIMING PATTERN
 * ============================================================================
 *
 * Similar to time_logger, this component uses millis() for non-blocking
 * periodic execution:
 * 1. Store last_feed_time_ when WDT is fed
 * 2. In loop(), calculate elapsed = millis() - last_feed_time_
 * 3. If elapsed >= feed_interval_, feed WDT and update last_feed_time_
 * 4. Return immediately to keep system responsive
 *
 * ============================================================================
 * SAFETY CONSIDERATIONS
 * ============================================================================
 *
 * - timeout should be long enough to handle normal system operations
 * - feed_interval should be much shorter than timeout (typical ratio: 1:20)
 * - For production: Disable test_mode to prevent intentional crashes
 * - WDT reset is a last resort - investigate root cause of hangs
 */
class WDTManager : public Component {
 public:
  /**
   * setup() - Initialize the hardware watchdog timer
   *
   * Called once at boot. Configures the ESP-IDF Task Watchdog Timer (TWDT)
   * with the specified timeout and subscribes the current task to monitoring.
   */
  void setup() override;

  /**
   * loop() - Non-blocking periodic WDT feeding
   *
   * Called continuously by ESPHome's main event loop. Feeds the WDT at
   * regular intervals to indicate healthy operation. In test mode, stops
   * feeding after crash_delay to simulate a system hang.
   */
  void loop() override;

  /**
   * feedWatchdog() - Reset the watchdog timer
   *
   * This public method feeds/resets the WDT, indicating the system is
   * functioning normally. Called automatically by loop(), but can also
   * be called manually by other components if needed.
   */
  void feedWatchdog();

  /**
   * Configuration setters (called by generated code from Python)
   */

  /**
   * set_timeout() - Set WDT timeout period in milliseconds
   *
   * @param timeout Milliseconds before WDT triggers reset (default: 10000)
   *
   * This is the maximum time the system can go without feeding the WDT
   * before a hardware reset is triggered.
   */
  void set_timeout(uint32_t timeout) {
    timeout_ = timeout;
  }

  /**
   * set_feed_interval() - Set WDT feeding interval in milliseconds
   *
   * @param interval Milliseconds between WDT feeds (default: 500)
   *
   * How often to reset the WDT during normal operation. Should be
   * significantly less than timeout to ensure reliable feeding.
   */
  void set_feed_interval(uint32_t interval) {
    feed_interval_ = interval;
  }

  /**
   * set_test_mode() - Enable/disable crash simulation
   *
   * @param enabled true to enable crash simulation (default: false)
   *
   * When enabled, WDT feeding stops after crash_delay to test the
   * automatic reset functionality.
   */
  void set_test_mode(bool enabled) {
    test_mode_ = enabled;
  }

  /**
   * set_crash_delay() - Set delay before simulated crash in test mode
   *
   * @param delay Milliseconds before crash simulation (default: 20000)
   *
   * In test mode, the component will stop feeding the WDT after this
   * delay to trigger an automatic reset.
   */
  void set_crash_delay(uint32_t delay) {
    crash_delay_ = delay;
  }

 private:
  // ===== Configuration Parameters =====

  /**
   * WDT timeout period in milliseconds.
   * Default: 10000ms (10 seconds)
   */
  uint32_t timeout_{10000};

  /**
   * Interval between WDT feeds in milliseconds.
   * Default: 500ms (0.5 seconds)
   */
  uint32_t feed_interval_{500};

  /**
   * Test mode flag - enables crash simulation.
   * Default: false
   */
  bool test_mode_{false};

  /**
   * Delay before simulated crash in test mode.
   * Default: 20000ms (20 seconds)
   */
  uint32_t crash_delay_{20000};

  // ===== Runtime State =====

  /**
   * Timestamp of last WDT feed (millis()).
   * Used for non-blocking periodic feeding.
   */
  uint32_t last_feed_time_{0};

  /**
   * Timestamp when component started (millis()).
   * Used to calculate elapsed time for crash simulation in test mode.
   */
  uint32_t start_time_{0};

  /**
   * Flag indicating if crash has been simulated.
   * Prevents repeated crash messages in test mode.
   */
  bool crash_simulated_{false};

  /**
   * Flag indicating if WDT has been successfully initialized.
   * Used to prevent feeding attempts before initialization.
   */
  bool wdt_initialized_{false};

#ifdef USE_ESP_IDF
  /**
   * WDT user handle for user-based subscription.
   * Using user-based subscription instead of task-based gives us
   * full control - we only feed our user, not the task.
   * This prevents idle tasks from interfering with timeout detection.
   */
  esp_task_wdt_user_handle_t wdt_user_handle_{nullptr};
#endif
};

} // namespace wdt_manager
} // namespace esphome
