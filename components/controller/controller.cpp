#include "controller.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace controller {

static const char *TAG = "controller.fsm";

void Controller::setup() {
  /**
   * FSM Initialization
   *
   * Set the initial state to INIT. This will show the boot sequence
   * (red → yellow → green) when the ESP32 starts.
   */
  this->current_state_ = &Controller::state_init;
  this->state_start_time_ = millis();
  this->state_counter_ = 0;

  /**
   * Initialize Central Status Logger
   *
   * Sets up the unified logging system for monitoring all critical
   * system variables (network, sensors, FSM state, alerts).
   */
  this->status_logger_.begin();
  this->last_status_log_time_ = millis();

  // Set initial logger state
  this->status_logger_.updateStatus(0.0, "INIT");
  this->status_logger_.updateIP("0.0.0.0");  // Will be updated when WiFi connects
  this->status_logger_.updateWebServerStatus(false, false);

  // Configure 420 mode if enabled
  this->status_logger_.set420Mode(this->mode_420_);

  /**
   * Register callback to receive sensor updates.
   *
   * WHY CALLBACK PATTERN (vs polling sensor->state in loop()):
   * - Push vs Pull: Sensor notifies us when new data arrives, we don't poll
   * - Efficiency: No wasted cycles checking if value changed
   * - Decoupling: Sensor controls update timing, controller just reacts
   * - Thread safety: Callback ensures atomic updates via lambda capture
   *
   * WHY LAMBDA:
   * - Captures 'this' pointer to access current_sensor_value_ member
   * - ESPHome sensor callbacks expect signature: void callback(float value)
   * - Lambda provides a clean way to adapt member function to C callback
   *
   * WHY nullptr CHECK:
   * - Safety: If YAML config omits sensor_source, we don't crash
   * - Allows controller to run in LED-only mode (though not useful)
   * - Defensive programming pattern common in ESPHome components
   */
  if (this->sensor_source_ != nullptr) {
    this->sensor_source_->add_on_state_callback([this](float x) {
      this->current_sensor_value_ = x;
      // Update status logger with new sensor value
      this->status_logger_.updateStatus(x, get_state_name(this->current_state_));
    });
  }

  // Publish initial state to text sensor (if configured)
  publish_state();

  // Log initial status immediately
  ESP_LOGI(TAG, "Controller initialized. Printing initial status:");
  this->status_logger_.logStatus();

  // Log verbose mode status
  if (this->verbose_) {
    ESP_LOGI(TAG, "Verbose mode: ENABLED - All actions and state transitions will be logged with timing");
  } else {
    ESP_LOGI(TAG, "Verbose mode: DISABLED");
  }
}

const char* Controller::get_state_name(StateHandler state) {
  if (state == &Controller::state_init) return "INIT";
  if (state == &Controller::state_calibration) return "CALIBRATION";
  if (state == &Controller::state_ready) return "READY";
  if (state == &Controller::state_error) return "ERROR";
  if (state == &Controller::state_error_test) return "ERROR_TEST";
  return "UNKNOWN";
}

void Controller::loop() {
  /**
   * FSM Driver - Core execution loop
   *
   * Called continuously by ESPHome's main event loop (~1000 Hz).
   *
   * FUNCTION POINTER CALL SYNTAX:
   * (this->*current_state_)() breaks down as:
   * - this->           = Object instance
   * - current_state_   = Member function pointer
   * - *                = Dereference the pointer
   * - ()               = Call with no arguments
   *
   * Returns StateFunc wrapper containing next state to execute.
   */
  // Track overall loop timing in verbose mode
  uint32_t loop_start_time = millis();

  if (this->current_state_ != nullptr) {
    // Time the state execution in verbose mode
    uint32_t state_exec_start = millis();

    // Execute current state handler, get next state
    StateFunc next_state = (this->*current_state_)();

    // Log state execution time if verbose and slow
    if (this->verbose_) {
      uint32_t state_exec_duration = millis() - state_exec_start;
      if (state_exec_duration >= 30) {
        ESP_LOGW(TAG, "[VERBOSE] SLOW state execution: %s (took %lums - EXCEEDS 30ms!)",
                 get_state_name(this->current_state_), state_exec_duration);
      } else if (state_exec_duration >= 10) {
        ESP_LOGI(TAG, "[VERBOSE] State execution: %s (took %lums)",
                 get_state_name(this->current_state_), state_exec_duration);
      }
    }

    /**
     * State Transition Logic
     *
     * WHY COMPARE FUNCTION POINTERS:
     * - States return themselves if they want to stay active
     * - States return a different state pointer to transition
     * - Simple pointer comparison detects when transition occurs
     *
     * WHAT HAPPENS ON TRANSITION:
     * 1. Log the transition (DEBUG level, filterable in production)
     * 2. Reset state_start_time_ to mark entry into new state
     * 3. Reset state_counter_ for stochastic transitions in new state
     * 4. Update current_state_ to the new state handler
     * 5. Publish new state to text sensor (if configured)
     * 6. Update status logger with new state
     *
     * WHY RESET TIMING:
     * Each state's timing is relative to when it was entered. Without
     * resetting state_start_time_, elapsed time calculations would be
     * incorrect and states would transition immediately.
     */
    if (next_state.func != this->current_state_) {
        const char* old_state_name = get_state_name(this->current_state_);
        const char* new_state_name = get_state_name(next_state.func);
        uint32_t time_in_state = millis() - this->state_start_time_;

        // Log state transition (with timing in verbose mode)
        if (this->verbose_) {
          ESP_LOGI(TAG, "[VERBOSE] State transition: %s -> %s (spent %lums in %s)",
                   old_state_name, new_state_name, time_in_state, old_state_name);
        } else {
          ESP_LOGI(TAG, "State transition to: %s", new_state_name);
        }

        this->state_start_time_ = millis();
        this->state_counter_ = 0; // Reset counter for the new state
        this->current_state_ = next_state.func;

        // Update status logger with new state
        this->status_logger_.updateStatus(this->current_sensor_value_, new_state_name);

        publish_state(); // Publish new state to text sensor
    }
  }

  /**
   * Periodic Status Logging (every 30 seconds)
   *
   * Non-blocking periodic logging using millis() comparison.
   * Prints comprehensive system status including:
   * - Current time (NTP-synchronized)
   * - Network status (IP, web server)
   * - Sensor readings
   * - FSM state
   * - Active alerts (if any)
   *
   * WHY 30 SECONDS:
   * - Frequent enough for monitoring without spamming logs
   * - Provides regular heartbeat confirmation
   * - Catches intermittent issues within reasonable timeframe
   */
  if (millis() - this->last_status_log_time_ >= this->status_log_interval_) {
    uint32_t status_log_start = millis();
    this->status_logger_.logStatus();
    this->last_status_log_time_ = millis();

    // Log status logging time if verbose and slow
    if (this->verbose_) {
      uint32_t status_log_duration = millis() - status_log_start;
      if (status_log_duration >= 30) {
        ESP_LOGW(TAG, "[VERBOSE] SLOW status logging (took %lums - EXCEEDS 30ms!)",
                 status_log_duration);
      } else if (status_log_duration >= 10) {
        ESP_LOGI(TAG, "[VERBOSE] Status logging (took %lums)", status_log_duration);
      }
    }
  }

  // Log overall loop time if verbose and slow
  if (this->verbose_) {
    uint32_t loop_duration = millis() - loop_start_time;
    if (loop_duration >= 30) {
      ESP_LOGW(TAG, "[VERBOSE] SLOW loop() execution (took %lums total - EXCEEDS 30ms!)",
               loop_duration);
    } else if (loop_duration >= 10) {
      ESP_LOGI(TAG, "[VERBOSE] loop() execution (took %lums total)", loop_duration);
    }
  }
}

// ============================================================================
// STATE IMPLEMENTATIONS
// ============================================================================
// State implementations are in separate files:
// - state_init.cpp: Boot sequence
// - state_calibration.cpp: Sensor stabilization
// - state_ready.cpp: Normal operation with breathing animation
// - state_error.cpp: Fault condition alert


// ============================================================================
// HELPER FUNCTION: apply_light
// ============================================================================
/**
 * Set LED color and brightness using ESPHome's light API.
 *
 * @param r Red component (0.0 to 1.0)
 * @param g Green component (0.0 to 1.0)
 * @param b Blue component (0.0 to 1.0)
 * @param brightness Overall brightness multiplier (0.0 to 1.0, default 1.0)
 *
 * ESPHOME LIGHT API PATTERN:
 * ESPHome uses a "builder" pattern for light control:
 * 1. make_call() creates a LightCall builder object
 * 2. set_state() / set_brightness() / set_rgb() configure the call
 * 3. perform() executes the command
 *
 * WHY BUILDER PATTERN:
 * - Allows setting multiple properties before sending to hardware
 * - Batches changes into single update (avoids flicker)
 * - Provides consistent API across different light types
 * - Enables transitions, effects, and other advanced features
 *
 * WHY brightness > 0.01f CHECK:
 * ESPHome lights have TWO brightness concepts:
 * - state: Boolean ON/OFF
 * - brightness: Float 0.0 to 1.0
 *
 * Setting brightness to 0.0 without setting state to false:
 * - Technically "on" but showing black
 * - Wastes power (LED driver still active)
 * - Web UI shows light as "on" (confusing)
 *
 * With brightness check:
 * - brightness > 0.01: state=true, light is ON
 * - brightness ≤ 0.01: state=false, light is OFF
 * - 0.01 threshold accounts for floating-point rounding errors
 *
 * WHY 0.01 (not exact 0.0):
 * - Floating-point math can produce 0.0000001 instead of 0.0
 * - 0.01 is small enough to be visually "off" (1% brightness)
 * - Safer than exact comparison with floating-point
 *
 * nullptr SAFETY:
 * If YAML config omits light_target, pointer is nullptr.
 * Early return prevents segfault. Allows controller to run in
 * sensor-only mode (though not useful in this application).
 */
void Controller::apply_light(float r, float g, float b, float brightness) {
  // Safety check: Do nothing if no light is connected
  if (this->light_target_ == nullptr) return;

  // Create a LightCall builder for configuring the light
  auto call = this->light_target_->make_call();

  // Set light state: ON if brightness > 1%, OFF otherwise
  call.set_state(brightness > 0.01f);

  // Set overall brightness (0.0 to 1.0)
  call.set_brightness(brightness);

  // Set RGB color components (0.0 to 1.0 each)
  call.set_rgb(r, g, b);

  // Execute the light command (sends to hardware)
  call.perform();
}

// ============================================================================
// HELPER FUNCTION: publish_state
// ============================================================================
/**
 * Publish current FSM state to the text sensor (if configured).
 *
 * WHY THIS METHOD:
 * - Allows monitoring FSM state via web UI, MQTT, Home Assistant, etc.
 * - Useful for debugging state transitions and system behavior
 * - Optional: Only publishes if state_text_ is configured in YAML
 *
 * WHEN CALLED:
 * - In setup() to publish initial state
 * - In loop() after each state transition
 *
 * nullptr SAFETY:
 * If state_text_ is not configured in YAML, it remains nullptr.
 * Early return prevents crashes and allows controller to work without
 * state publishing.
 */
void Controller::publish_state() {
  // Safety check: Do nothing if no text sensor is connected
  if (this->state_text_ == nullptr) return;

  // Get the human-readable name of the current state
  const char* state_name = get_state_name(this->current_state_);

  // Publish the state to the text sensor
  this->state_text_->publish_state(state_name);
}

// ============================================================================
// PUBLIC API: reset_to_init
// ============================================================================
/**
 * Manually force a reset to INIT state.
 *
 * WHY THIS METHOD:
 * - Allows external components (buttons, services, automations) to reset the FSM
 * - Useful for testing: Can trigger state transitions on demand
 * - Useful for debugging: Can recover from stuck states
 * - Useful for users: Manual reset button in web UI
 *
 * IMPLEMENTATION:
 * - Transitions to INIT state immediately
 * - Resets timing counters as if the state was naturally entered
 * - Publishes state change to text sensor
 * - Logs the manual reset for debugging
 *
 * SAFETY:
 * - Can be called at any time from any state
 * - No side effects beyond state transition
 * - Thread-safe (called from ESPHome main loop)
 */
void Controller::reset_to_init() {
  ESP_LOGI(TAG, "Manual reset to INIT state requested");

  // Transition to INIT state
  this->current_state_ = &Controller::state_init;

  // Reset timing counters
  this->state_start_time_ = millis();
  this->state_counter_ = 0;

  // Publish the new state
  publish_state();
}

// ============================================================================
// PUBLIC API: trigger_error_test
// ============================================================================
/**
 * Manually trigger ERROR_TEST state.
 *
 * WHY THIS METHOD:
 * - Allows PSMChecker and other test components to trigger ERROR_TEST state
 * - Used for testing persistent state recovery after power loss
 * - Visual indication (blue/purple LED) distinguishes from normal ERROR
 *
 * IMPLEMENTATION:
 * - Transitions to ERROR_TEST state immediately
 * - Resets timing counters
 * - Publishes state change to text sensor
 * - Logs the transition
 *
 * USAGE:
 * - Called by PSMChecker before prompting user to unplug device
 * - Ensures system is in known test state before power cycle
 */
void Controller::trigger_error_test() {
  ESP_LOGI(TAG, "Triggering ERROR_TEST state for PSM testing");

  // Transition to ERROR_TEST state
  this->current_state_ = &Controller::state_error_test;

  // Reset timing counters
  this->state_start_time_ = millis();
  this->state_counter_ = 0;

  // Publish the new state
  publish_state();

  // Update status logger
  this->status_logger_.updateStatus(this->current_sensor_value_, "ERROR_TEST");
}

// ============================================================================
// PUBLIC API: toggle_verbose
// ============================================================================
/**
 * Toggle verbose logging mode on/off at runtime.
 *
 * WHY THIS METHOD:
 * - Allows runtime control of logging verbosity without reflashing
 * - Useful for debugging: Enable verbose mode when investigating issues
 * - Reduces log spam in production: Keep verbose mode off normally
 * - Can be triggered via web UI button, API call, or automation
 *
 * IMPLEMENTATION:
 * - Flips the verbose_ flag
 * - Logs the mode change at INFO level (always visible)
 * - Immediate effect on all subsequent state transitions and actions
 *
 * SAFETY:
 * - Can be called at any time from any state
 * - No side effects beyond logging behavior change
 * - Thread-safe (called from ESPHome main loop)
 */
void Controller::toggle_verbose() {
  this->verbose_ = !this->verbose_;

  if (this->verbose_) {
    ESP_LOGI(TAG, "Verbose mode ENABLED - Detailed logging active");
  } else {
    ESP_LOGI(TAG, "Verbose mode DISABLED - Minimal logging active");
  }
}

// ============================================================================
// PUBLIC API: blaze_it
// ============================================================================
/**
 * Manually trigger 420 ASCII art display.
 *
 * WHY THIS METHOD:
 * - Allows web UI button to display easter egg on demand
 * - Fun user interaction without waiting for 4:20 AM/PM
 * - Direct access to status logger's ASCII art method
 *
 * IMPLEMENTATION:
 * - Calls status_logger_.print420Art() immediately
 * - No time checking - displays art whenever called
 * - Safe to call at any time from any state
 *
 * USAGE:
 * - Triggered by "Blaze It" button in web UI
 * - Can also be called from automations or services
 */
void Controller::blaze_it() {
  ESP_LOGI(TAG, "Blaze It button pressed!");
  this->status_logger_.print420Art();
}

// ============================================================================
// HELPER FUNCTIONS: Verbose Mode Logging
// ============================================================================

/**
 * Begin timing an action for verbose logging.
 *
 * @param action_name Description of the action being performed
 *
 * Records the start time and logs the action name if verbose mode is enabled.
 * Should be paired with log_action_end() to measure duration.
 */
void Controller::log_action_start(const char* action_name) {
  if (!this->verbose_) return;

  this->action_start_time_ = millis();
  // Use DEBUG level for action start (less important than duration)
  ESP_LOGD(TAG, "[VERBOSE] Action started: %s", action_name);
}

/**
 * Log completion of an action with duration.
 *
 * @param action_name Description of the action (should match log_action_start)
 *
 * Calculates the time elapsed since log_action_start() was called and logs
 * the action completion with duration if verbose mode is enabled.
 *
 * LOG LEVELS:
 * - >= 30ms: WARN (ESPHome recommends max 30ms per loop iteration)
 * - >= 10ms: INFO (noticeable delay)
 * - < 10ms: DEBUG (normal operation)
 */
void Controller::log_action_end(const char* action_name) {
  if (!this->verbose_) return;

  uint32_t duration = millis() - this->action_start_time_;

  // Use different log levels based on duration
  if (duration >= 30) {
    ESP_LOGW(TAG, "[VERBOSE] SLOW Action: %s (took %lums - EXCEEDS 30ms recommendation!)",
             action_name, duration);
  } else if (duration >= 10) {
    ESP_LOGI(TAG, "[VERBOSE] Action: %s (took %lums)", action_name, duration);
  } else {
    ESP_LOGD(TAG, "[VERBOSE] Action: %s (took %lums)", action_name, duration);
  }
}

} // namespace controller
} // namespace esphome
