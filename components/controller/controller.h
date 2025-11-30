#pragma once
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "CentralStatusLogger.h"

namespace esphome {
namespace controller {

/**
 * Controller: Finite State Machine for Plant Monitoring System
 *
 * ============================================================================
 * FSM ARCHITECTURE OVERVIEW
 * ============================================================================
 *
 * This controller implements a function-pointer-based FSM that manages system
 * states and provides visual feedback via an RGB LED.
 *
 * STATE DIAGRAM:
 *
 *                    ┌─────────────┐
 *                    │    INIT     │  Boot sequence: Red → Yellow → Green
 *                    │ (3 seconds) │  Shows system is starting up
 *                    └──────┬──────┘
 *                           │ after 3s
 *                           ▼
 *                    ┌─────────────┐
 *                    │ CALIBRATION │  Blinking yellow for 4 seconds
 *                    │ (4 seconds) │  Simulates sensor calibration period
 *                    └──────┬──────┘
 *                           │ after 4s
 *                           ▼
 *                    ┌─────────────┐
 *              ┌────▶│    READY    │  Breathing green animation
 *              │     │  (ongoing)  │  Normal operation mode
 *              │     └──────┬──────┘
 *              │            │ if sensor value >90
 *              │            ▼
 *              │     ┌─────────────┐
 *              │     │    ERROR    │  Fast red flashing
 *              └─────┤ (5 seconds) │  Shows fault condition
 *                    └─────────────┘
 *                      │ after 5s
 *                      └──▶ returns to INIT (full restart)
 *
 * ============================================================================
 * WHY FUNCTION POINTER FSM (vs switch/case or state pattern)
 * ============================================================================
 *
 * 1. PERFORMANCE: Direct function pointer calls are faster than switch/case
 *    with many states. Critical for loop() running at ~1000 Hz.
 *
 * 2. MEMORY EFFICIENCY: No vtable overhead from virtual functions (state pattern).
 *    Important on constrained ESP32 with limited RAM.
 *
 * 3. CLEAN STATE TRANSITIONS: Each state returns the next state to execute.
 *    Transitions are explicit and easy to trace: "return &Controller::state_ready"
 *
 * 4. NO GLOBAL STATE TABLE: Switch/case requires a separate mapping of state
 *    enum → handler. Function pointers eliminate this indirection.
 *
 * TRADE-OFFS:
 * - Less type-safe than enum-based FSMs (function pointer could be null)
 * - Harder to visualize state transitions (no central state table)
 * - We mitigate these with nullptr checks and comprehensive documentation
 *
 * ============================================================================
 * FSM TIMING ARCHITECTURE
 * ============================================================================
 *
 * Each state manages its own timing using:
 * - state_start_time_: Timestamp when state was entered (millis())
 * - elapsed = millis() - state_start_time_: Time spent in current state
 *
 * WHY NOT use delay():
 * - delay() blocks the entire ESP32 event loop
 * - WiFi, OTA, web server, other components would freeze
 * - Non-blocking timing via millis() keeps the system responsive
 *
 * WHY NOT use PollingComponent:
 * - FSM needs to run every loop iteration for smooth animations
 * - PollingComponent adds overhead for timing we don't need
 * - State transitions need millisecond precision, not second precision
 *
 * ============================================================================
 * SENSOR THRESHOLD MONITORING (state_counter_ pattern)
 * ============================================================================
 *
 * The READY state monitors sensor values and transitions to ERROR if the
 * threshold is exceeded.
 *
 * WHY state_counter_:
 * loop() runs ~1000 times per second. Without state_counter_, we'd check
 * the threshold 1000 times per second and spam error transitions. state_counter_
 * tracks which second we're in and only checks once per second.
 *
 * THRESHOLD:
 * - High threshold: >90 (e.g., over-watering, sensor malfunction)
 * - Creates observable alerts based on actual sensor data
 *
 * ============================================================================
 * COMPONENT LIFECYCLE
 * ============================================================================
 *
 * setup():  Called once at boot
 *           - Initialize FSM to INIT state
 *           - Register sensor callback to receive data updates
 *           - Seed random number generator
 *
 * loop():   Called continuously (~1000 Hz) by ESPHome main loop
 *           - Execute current state handler
 *           - Check if state wants to transition
 *           - Update current_state_ and reset timing if transitioning
 *           - State handlers update LED via apply_light()
 */
class Controller : public Component {
 public:
  /**
   * setup() - Initialize the FSM and register sensor callback
   */
  void setup() override;

  /**
   * loop() - FSM driver, executes current state and handles transitions
   *
   * Called continuously by ESPHome's main event loop. Runs at high frequency
   * (~1000 Hz) to maintain smooth LED animations.
   */
  void loop() override;

  /**
   * Dependency injection setters (called by generated code from Python)
   */
  void set_sensor_source(sensor::Sensor *s) { sensor_source_ = s; }
  void set_light_target(light::LightState *l) { light_target_ = l; }
  void set_state_text(text_sensor::TextSensor *t) { state_text_ = t; }

  /**
   * reset_to_init() - Manually trigger a reset to INIT state
   *
   * PUBLIC API for external components (e.g., button actions) to force
   * the controller back to INIT state. Useful for:
   * - Testing state transitions
   * - Debugging FSM behavior
   * - Manual recovery from ERROR state
   * - User-initiated recalibration
   */
  void reset_to_init();

  /**
   * get_logger() - Access the central status logger
   *
   * PUBLIC API to access the status logger for external components.
   * Allows other components to update logger state (IP, web server status, etc.)
   * or trigger custom alerts.
   *
   * @return Pointer to the CentralStatusLogger instance
   */
  CentralStatusLogger* get_logger() { return &status_logger_; }

  /**
   * trigger_error_test() - Manually trigger ERROR_TEST state
   *
   * PUBLIC API for testing components (like PSMChecker) to trigger
   * the ERROR_TEST state. This is a special test state that:
   * - Shows blue/purple LED pattern
   * - Stays in ERROR_TEST until manually reset
   * - Used for testing persistent state recovery
   */
  void trigger_error_test();

 private:
  // ===== Component Dependencies (injected via setters) =====

  /**
   * Pointer to sensor providing input data.
   *
   * WHY POINTER (not reference):
   * - Can be nullptr, allowing nullptr checks for safety
   * - Matches ESPHome's component linking pattern
   * - Can be reassigned if needed (though we don't in this implementation)
   */
  sensor::Sensor *sensor_source_{nullptr};

  /**
   * Pointer to light component for visual output.
   *
   * WHY light::LightState (not specific LED type):
   * - Works with any ESPHome light: addressable, RGB, RGBW, monochrome
   * - Loose coupling: controller doesn't care about LED hardware details
   * - LightState provides a consistent API across all light types
   */
  light::LightState *light_target_{nullptr};

  /**
   * Optional pointer to text sensor for publishing FSM state.
   *
   * WHY OPTIONAL (nullptr-safe):
   * - Not required for core functionality (visual feedback via LED)
   * - Useful for debugging and monitoring via web UI/MQTT
   * - Allows users to opt-in to state publishing
   */
  text_sensor::TextSensor *state_text_{nullptr};

  /**
   * Cached sensor value updated via callback.
   *
   * WHY CACHED (vs reading sensor->state directly in states):
   * - Callback pattern ensures we always have the latest value
   * - Atomic update (no risk of reading mid-update)
   * - Future-proofing: allows preprocessing/filtering before states see it
   */
  float current_sensor_value_{0.0};

  // ===== FSM Infrastructure =====

  /**
   * Forward declaration for StateFunc wrapper.
   *
   * WHY WRAPPER STRUCT (vs bare function pointer):
   * - Allows implicit conversion from function pointers
   * - Makes state handler return type more readable
   * - Could be extended with state metadata (name, ID) if needed
   *
   * ALTERNATIVE CONSIDERED:
   * Could use enum State + switch/case, but function pointers are faster
   * and avoid the need for a state→handler mapping table.
   */
  struct StateFunc;

  /**
   * Type alias for member function pointer to state handler.
   *
   * Syntax breakdown: StateFunc (Controller::*)()
   * - Controller::  = Member of Controller class
   * - *             = Pointer to member
   * - ()            = Function taking no arguments
   * - StateFunc     = Returns StateFunc wrapper
   */
  using StateHandler = StateFunc (Controller::*)();

  /**
   * Wrapper struct for state handler function pointers.
   *
   * Allows state handlers to simply "return &Controller::state_name"
   * instead of "return StateFunc(&Controller::state_name)".
   */
  struct StateFunc {
    StateHandler func;
    StateFunc(StateHandler f) : func(f) {}
  };

  /**
   * Current active state (function pointer).
   *
   * WHY nullptr-initialized:
   * - Allows detecting uninitialized FSM (safety check in loop())
   * - setup() will set this to &Controller::state_init
   */
  StateHandler current_state_{nullptr};

  /**
   * Timestamp (millis()) when current state was entered.
   *
   * Used by states to calculate elapsed time for animations and transitions.
   * Reset to millis() whenever a state transition occurs.
   */
  uint32_t state_start_time_{0};

  /**
   * Event counter within current state.
   *
   * WHY NEEDED (threshold checking pattern):
   * loop() runs ~1000 times per second. For events that should happen once
   * per second (like sensor threshold checks), we use state_counter_ to track
   * which second we're in. Only increment and check when the second changes.
   *
   * USAGE PATTERN (in state_ready):
   *   uint32_t current_seconds = elapsed / 1000;
   *   if (current_seconds > this->state_counter_) {
   *     this->state_counter_ = current_seconds;
   *     // Now check sensor threshold (only once this second)
   *   }
   *
   * Reset to 0 on every state transition.
   */
  uint32_t state_counter_{0};

  // ===== Status Logger =====

  /**
   * Central status logger for unified system monitoring.
   *
   * Provides comprehensive logging of all critical system variables:
   * - Network status (IP address, web server state)
   * - Sensor readings (pH, temperature, etc.)
   * - FSM state (current active routine)
   * - Critical alerts (spill, sensor out of range, etc.)
   *
   * Logs complete status report every 30 seconds to serial monitor.
   */
  CentralStatusLogger status_logger_;

  /**
   * Timestamp for 30-second periodic status logging.
   *
   * Tracks when the last status report was printed. Used with millis()
   * for non-blocking periodic logging (every 30000ms).
   */
  uint32_t last_status_log_time_{0};

  // ===== State Handler Functions =====

  /**
   * INIT state: Boot sequence showing red → yellow → green over 3 seconds.
   *
   * PURPOSE: Visual confirmation that the system is starting up.
   * DURATION: 3 seconds
   * VISUAL: Solid colors transitioning every 1 second
   * NEXT STATE: CALIBRATION
   */
  StateFunc state_init();

  /**
   * CALIBRATION state: Blinking yellow for 4 seconds.
   *
   * PURPOSE: Simulates sensor calibration period. In production, this could
   *          perform actual sensor stabilization (e.g., soil moisture sensor
   *          needs time to stabilize after power-on).
   * DURATION: 4 seconds
   * VISUAL: Yellow blinking at 1 Hz (500ms on, 500ms off)
   * NEXT STATE: READY
   */
  StateFunc state_calibration();

  /**
   * READY state: Normal operation with breathing green animation.
   *
   * PURPOSE: Indicates system is operating normally and monitoring sensors.
   * DURATION: Indefinite (until sensor threshold exceeded)
   * VISUAL: Breathing green (sine wave brightness modulation)
   * THRESHOLD MONITORING: Triggers ERROR if sensor value >90
   * NEXT STATE: ERROR (when threshold exceeded) or stay in READY
   */
  StateFunc state_ready();

  /**
   * ERROR state: Fast red flashing for 5 seconds, then restart.
   *
   * PURPOSE: Visual alert for fault conditions triggered by sensor threshold
   *          violation (value >90). Could also be extended for connectivity
   *          issues or other system faults.
   * DURATION: 5 seconds
   * VISUAL: Red flashing at 5 Hz (100ms on, 100ms off) - very attention-grabbing
   * NEXT STATE: INIT (performs full restart sequence)
   */
  StateFunc state_error();

  /**
   * ERROR_TEST state: Blue/purple LED pattern for PSM testing.
   *
   * PURPOSE: Special test state for validating persistent state recovery.
   *          Triggered by PSMChecker to test NVS persistence.
   * DURATION: Indefinite (stays in this state until reset)
   * VISUAL: Pulsing blue/purple to distinguish from normal ERROR state
   * NEXT STATE: Stays in ERROR_TEST (requires manual reset or power cycle)
   */
  StateFunc state_error_test();

  // ===== Helper Functions =====

  /**
   * get_state_name() - Convert state handler function pointer to readable name
   *
   * @param state State handler function pointer
   * @return String name of the state (e.g., "INIT", "READY")
   *
   * Used for logging state transitions with human-readable names.
   */
  const char* get_state_name(StateHandler state);

  /**
   * apply_light() - Set LED color and brightness
   *
   * @param r Red component (0.0 to 1.0)
   * @param g Green component (0.0 to 1.0)
   * @param b Blue component (0.0 to 1.0)
   * @param brightness Overall brightness (0.0 to 1.0, default 1.0)
   *
   * WHY THIS ABSTRACTION:
   * - Encapsulates the ESPHome light API (make_call() pattern)
   * - Provides consistent interface for all state handlers
   * - Handles nullptr safety check in one place
   * - Could be extended with color correction, gamma curves, etc.
   *
   * WHY brightness > 0.01 CHECK (in implementation):
   * - ESPHome lights have a boolean "state" (on/off) separate from brightness
   * - Setting brightness to 0.0 should turn the light off, not just dim to black
   * - 0.01 threshold avoids floating-point precision issues (0.0001 wouldn't
   *   be visible anyway, but we want the light explicitly off)
   */
  void apply_light(float r, float g, float b, float brightness = 1.0);

  /**
   * publish_state() - Publish current FSM state to text sensor
   *
   * WHY SEPARATE METHOD:
   * - Encapsulates text sensor publishing logic
   * - Provides nullptr safety in one place
   * - Called on state transitions to update web UI
   * - Allows state to be monitored via MQTT/Home Assistant
   */
  void publish_state();
};

} // namespace controller
} // namespace esphome
