#include "wdt_manager.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

// ESP-IDF Task Watchdog Timer API and FreeRTOS
#ifdef USE_ESP_IDF
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace esphome {
namespace wdt_manager {

static const char *TAG = "wdt_manager";

void WDTManager::setup() {
  /**
   * Initialize the hardware watchdog timer.
   *
   * This function configures the ESP-IDF Task Watchdog Timer (TWDT) with
   * the specified timeout and subscribes the current task to monitoring.
   *
   * INITIALIZATION STEPS:
   * 1. Convert timeout from milliseconds to seconds (TWDT expects seconds)
   * 2. Initialize TWDT with timeout and panic mode enabled
   * 3. Subscribe current task (main ESPHome loop) to TWDT monitoring
   * 4. Set initialization flag and record start time
   * 5. Log success or error status
   *
   * ERROR HANDLING:
   * If initialization fails, the component will log an error and disable
   * WDT feeding to prevent runtime errors.
   */

#ifdef USE_ESP_IDF
  // Ensure minimum timeout of 1 second (1000ms)
  if (this->timeout_ < 1000) {
    this->timeout_ = 1000;
    ESP_LOGW(TAG, "Timeout too short, using minimum of 1000ms");
  }

  ESP_LOGI(TAG, "Initializing Hardware Watchdog Timer (Timeout: %u ms)", this->timeout_);

  /**
   * Initialize or Reconfigure the Task Watchdog Timer
   *
   * ESP-IDF 5.x uses a configuration structure for WDT initialization.
   *
   * esp_task_wdt_config_t fields:
   * - timeout_ms: Timeout period in milliseconds
   * - idle_core_mask: Bitmask of cores to monitor (0 = no idle task monitoring)
   * - trigger_panic: Enable panic mode (triggers panic and reset on timeout)
   *
   * WHY PANIC MODE:
   * - Ensures hardware reset on timeout (not just a warning)
   * - Provides critical safety for production systems
   * - Guarantees recovery from complete system hangs
   *
   * WHY NO IDLE MONITORING (idle_core_mask = 0):
   * - We monitor specific tasks, not idle cores
   * - Prevents false triggers when CPU is legitimately busy
   * - More precise monitoring of critical application tasks
   *
   * HANDLING EXISTING TWDT:
   * ESP-IDF often auto-initializes TWDT during boot. If initialization
   * fails with ESP_ERR_INVALID_STATE (already initialized), we reconfigure
   * it instead of failing.
   */
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = this->timeout_,       // Timeout in milliseconds
    .idle_core_mask = 0,                // Don't monitor idle tasks
    .trigger_panic = true               // Trigger panic on timeout (causes reset)
  };

  esp_err_t err = esp_task_wdt_init(&wdt_config);

  // If TWDT is already initialized, reconfigure it
  if (err == ESP_ERR_INVALID_STATE) {
    ESP_LOGI(TAG, "WDT already initialized, reconfiguring...");
    err = esp_task_wdt_reconfigure(&wdt_config);

    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to reconfigure WDT: %d", err);
      this->wdt_initialized_ = false;
      return;
    }
    ESP_LOGI(TAG, "WDT reconfigured (timeout: %u ms, panic mode: enabled)", this->timeout_);
  } else if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize WDT: %d", err);
    this->wdt_initialized_ = false;
    return;
  } else {
    ESP_LOGI(TAG, "WDT initialized (timeout: %u ms, panic mode: enabled)", this->timeout_);
  }

  /**
   * Subscribe a USER (not task) to WDT monitoring
   *
   * WHY USER-BASED SUBSCRIPTION:
   * - Task-based subscription monitors the entire task (including system calls)
   * - Idle tasks auto-subscribed by ESP-IDF keep feeding the WDT
   * - User-based subscription gives us EXCLUSIVE control
   * - Only our code feeds the WDT, so stopping feed WILL trigger timeout
   *
   * esp_task_wdt_add_user() creates a named user subscription.
   * We must call esp_task_wdt_reset_user() with this handle to feed.
   */
  err = esp_task_wdt_add_user("wdt_manager", &this->wdt_user_handle_);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add WDT user: %d", err);
    this->wdt_initialized_ = false;
    return;
  }

  ESP_LOGI(TAG, "WDT user 'wdt_manager' subscribed successfully");

  // WDT successfully initialized/reconfigured and task subscribed
  this->wdt_initialized_ = true;
  this->start_time_ = millis();
  this->last_feed_time_ = millis();

  ESP_LOGI(TAG, "Feed interval: %u ms", this->feed_interval_);
  ESP_LOGI(TAG, "IMPORTANT: Panic mode enabled - WDT timeout WILL cause device reset");

  if (this->test_mode_) {
    ESP_LOGW(TAG, "TEST MODE ENABLED - Will simulate crash after %u seconds",
             this->crash_delay_ / 1000);
  }

#else
  // Not using ESP-IDF framework
  ESP_LOGE(TAG, "WDT Manager requires ESP-IDF framework");
  this->wdt_initialized_ = false;
#endif
}

void WDTManager::loop() {
  /**
   * Non-blocking periodic WDT feeding and crash simulation.
   *
   * This function is called continuously by ESPHome's main event loop.
   * It handles:
   * 1. Regular WDT feeding at feed_interval
   * 2. Crash simulation in test mode after crash_delay
   *
   * TIMING PATTERN:
   * Similar to time_logger, we use millis() for non-blocking timing:
   * - Store last_feed_time_ when WDT is fed
   * - Calculate elapsed = millis() - last_feed_time_
   * - If elapsed >= feed_interval_, feed WDT
   *
   * TEST MODE BEHAVIOR:
   * When test_mode_ is true:
   * - Feed WDT normally for crash_delay_ milliseconds
   * - After crash_delay_, stop feeding and log "Simulated Crash"
   * - WDT timeout will trigger automatic reset
   */

  // Skip if WDT not initialized
  if (!this->wdt_initialized_) {
    return;
  }

  // Calculate elapsed time since component start
  uint32_t current_time = millis();
  uint32_t elapsed_since_start = current_time - this->start_time_;

  /**
   * Test Mode: Crash Simulation
   *
   * After crash_delay_ has elapsed, stop feeding the WDT to simulate
   * a system hang. This tests the automatic reset functionality.
   */
  if (this->test_mode_ && elapsed_since_start >= this->crash_delay_) {
    // Only log crash message once
    if (!this->crash_simulated_) {
      ESP_LOGW(TAG, "===== SIMULATED CRASH =====");
      ESP_LOGW(TAG, "Stopping WDT feeding to test automatic reset");
      ESP_LOGW(TAG, "Device should reboot in ~%u seconds", this->timeout_ / 1000);
      this->crash_simulated_ = true;
    }

    // STOP FEEDING - This will trigger WDT timeout and reset
    return;
  }

  /**
   * Normal Operation: Regular WDT Feeding
   *
   * Feed the WDT at regular intervals (feed_interval_) to indicate
   * the system is healthy and responsive.
   */
  uint32_t elapsed_since_feed = current_time - this->last_feed_time_;

  if (elapsed_since_feed >= this->feed_interval_) {
    // Feed the watchdog
    this->feedWatchdog();

    // Update last feed time
    this->last_feed_time_ = current_time;

    // Log feeding activity (helps monitor WDT operation)
    // In test mode, also show time remaining until crash
    if (this->test_mode_) {
      uint32_t time_to_crash = this->crash_delay_ - elapsed_since_start;
      ESP_LOGI(TAG, "WDT fed (time to crash: %u s)", time_to_crash / 1000);
    } else {
      ESP_LOGI(TAG, "WDT fed");
    }
  }
}

void WDTManager::feedWatchdog() {
  /**
   * Reset the hardware watchdog timer.
   *
   * This function calls esp_task_wdt_reset() to indicate the current task
   * is functioning normally. This must be called regularly (before timeout
   * expires) to prevent an automatic reset.
   *
   * PUBLIC ACCESS:
   * This method is public so other components can manually feed the WDT
   * if needed (e.g., during long-running operations).
   *
   * SAFETY:
   * Only feeds if WDT has been successfully initialized to prevent errors.
   */

#ifdef USE_ESP_IDF
  if (!this->wdt_initialized_) {
    return;
  }

  /**
   * Reset the Task Watchdog Timer (USER-BASED)
   *
   * esp_task_wdt_reset_user() parameters:
   * - wdt_user_handle_: Our user handle from esp_task_wdt_add_user()
   *
   * CRITICAL DIFFERENCE:
   * - esp_task_wdt_reset() resets for the TASK (all users in that task)
   * - esp_task_wdt_reset_user() resets ONLY for our specific user
   * - Idle tasks continue running/feeding their own subscriptions
   * - But our user subscription is independent
   * - When WE stop feeding, WDT times out even if idle tasks are feeding
   *
   * EFFECT:
   * - Resets the WDT counter to 0 for our user
   * - System has another timeout period before reset
   * - Should be called regularly (every feed_interval_)
   */
  if (this->wdt_user_handle_ == nullptr) {
    ESP_LOGE(TAG, "WDT user handle is null, cannot reset");
    return;
  }

  esp_err_t err = esp_task_wdt_reset_user(this->wdt_user_handle_);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to reset WDT user: %d", err);
  }
#endif
}

} // namespace wdt_manager
} // namespace esphome
