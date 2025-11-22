#include "controller.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cmath>

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
    });
  }
}

const char* Controller::get_state_name(StateHandler state) {
  if (state == &Controller::state_init) return "INIT";
  if (state == &Controller::state_calibration) return "CALIBRATION";
  if (state == &Controller::state_ready) return "READY";
  if (state == &Controller::state_error) return "ERROR";
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
  if (this->current_state_ != nullptr) {
    // Execute current state handler, get next state
    StateFunc next_state = (this->*current_state_)();

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
     *
     * WHY RESET TIMING:
     * Each state's timing is relative to when it was entered. Without
     * resetting state_start_time_, elapsed time calculations would be
     * incorrect and states would transition immediately.
     */
    if (next_state.func != this->current_state_) {
        ESP_LOGI(TAG, "State transition to: %s", get_state_name(next_state.func));
        this->state_start_time_ = millis();
        this->state_counter_ = 0; // Reset counter for the new state
        this->current_state_ = next_state.func;
    }
  }
}

// ============================================================================
// STATE: INIT - Boot Sequence
// ============================================================================
/**
 * Visual boot sequence: Red (1s) → Yellow (1s) → Green (1s) → CALIBRATION
 *
 * PURPOSE:
 * - Visual confirmation that the device is booting
 * - Allows user to verify LED is working during startup
 * - Traditional traffic light pattern is universally recognizable
 *
 * TIMING BREAKDOWN:
 * - 0-1000ms:   Red    (danger/stop - system initializing)
 * - 1000-2000ms: Yellow (caution - system starting)
 * - 2000-3000ms: Green  (ready - system about to enter calibration)
 * - 3000ms+:    Transition to CALIBRATION
 *
 * WHY THESE COLORS:
 * - Red:    Universally associated with "starting up" or "not ready"
 * - Yellow: Transitional state, "getting ready"
 * - Green:  "Go" signal, transitioning to active operation
 *
 * WHY 1 SECOND INTERVALS:
 * - Fast enough to keep boot time reasonable (3s total)
 * - Slow enough for humans to clearly see each color
 * - Matches user expectations from other devices (routers, etc.)
 */
Controller::StateFunc Controller::state_init() {
  uint32_t elapsed = millis() - this->state_start_time_;

  // Time-based color selection using simple cascading if-else
  // WHY NOT switch/case: elapsed is continuous, not discrete values
  if (elapsed < 1000) {
    apply_light(1.0, 0.0, 0.0); // Red (full intensity)
  } else if (elapsed < 2000) {
    apply_light(1.0, 1.0, 0.0); // Yellow (red + green = yellow)
  } else if (elapsed < 3000) {
    apply_light(0.0, 1.0, 0.0); // Green (full intensity)
  } else {
    // After 3 seconds, proceed to calibration
    return &Controller::state_calibration;
  }

  // Stay in INIT state (called every loop iteration, ~1000 Hz)
  return &Controller::state_init;
}

// ============================================================================
// STATE: CALIBRATION - Sensor Stabilization
// ============================================================================
/**
 * Blinking yellow pattern for 4 seconds, then transition to READY.
 *
 * PURPOSE:
 * - Simulates sensor calibration/stabilization period
 * - In production, this state could perform actual sensor warmup:
 *   * Soil moisture sensors need time to stabilize readings
 *   * Temperature sensors may need thermal equilibration
 *   * Air quality sensors often require 30-60s warmup
 * - Visual feedback that system is preparing, not ready yet
 *
 * BLINKING PATTERN:
 * - 500ms ON / 500ms OFF = 1 Hz blink rate
 * - Color: Amber (r=1.0, g=0.8, b=0.0) - warmer than pure yellow
 * - Duration: 4 seconds (8 blinks total)
 *
 * WHY 500ms INTERVALS:
 * - 1 Hz is standard for "busy" or "processing" indicators
 * - Fast enough to show activity, slow enough to not induce seizures
 * - Matches user expectations from other devices
 *
 * WHY 4 SECONDS TOTAL:
 * - Long enough to be noticeable (not skipped too fast)
 * - Short enough to not annoy users
 * - In production, actual calibration time would be sensor-dependent
 *
 * BLINKING MATH:
 * elapsed / 500 gives number of 500ms intervals passed
 * % 2 alternates between 0 (even) and 1 (odd)
 * even intervals = ON, odd intervals = OFF
 */
Controller::StateFunc Controller::state_calibration() {
  uint32_t elapsed = millis() - this->state_start_time_;

  // Blink at 1 Hz (500ms period)
  bool on = (elapsed / 500) % 2 == 0;
  if (on) apply_light(1.0, 0.8, 0.0, 1.0);  // Amber (warm yellow)
  else    apply_light(0.0, 0.0, 0.0, 0.0);  // Off (brightness = 0)

  // After 4 seconds, transition to READY state
  if (elapsed > 4000) {
    return &Controller::state_ready;
  }

  // Stay in CALIBRATION state
  return &Controller::state_calibration;
}

// ============================================================================
// STATE: READY - Normal Operation
// ============================================================================
/**
 * Normal operating state with breathing green animation and sensor monitoring.
 *
 * PURPOSE:
 * - Indicates system is functioning normally and monitoring sensors
 * - Breathing effect shows system is "alive" (not frozen)
 * - Sensor threshold monitoring triggers ERROR state when exceeded
 *
 * VISUAL: Breathing Green Animation
 * - Base color: Pure green (r=0.0, g=1.0, b=0.0)
 * - Brightness: Sine wave modulation from 10% to 100%
 * - Period: ~1 second per breath cycle
 * - Effect: "Inhale/exhale" pulsing
 *
 * SENSOR MONITORING:
 * - Checks sensor value against threshold once per second
 * - High threshold (>90): Triggers ERROR state
 * - Provides real sensor-based alerts instead of random errors
 *
 * TWO-PHASE IMPLEMENTATION:
 * 1. Transition logic (checked once per second)
 * 2. Visual logic (updated every loop iteration for smooth animation)
 */
Controller::StateFunc Controller::state_ready() {
  uint32_t elapsed = millis() - this->state_start_time_;
  uint32_t current_seconds = elapsed / 1000;

  // =========================================================================
  // PHASE 1: Stochastic Transition Logic
  // =========================================================================
  /**
   * Check sensor threshold ONCE PER SECOND.
   *
   * WHY ONCE PER SECOND (not every loop iteration):
   * - loop() runs ~1000 times per second
   * - Without gating, we'd trigger errors 1000 times per second
   * - state_counter_ tracks which second we're in, only checks on new seconds
   * - Prevents error state spam when threshold is consistently violated
   *
   * HOW state_counter_ WORKS:
   * - current_seconds = elapsed / 1000 converts ms to seconds
   * - When current_seconds > state_counter_, a new second has started
   * - We update state_counter_ and perform threshold check
   * - Subsequent loop iterations in the same second skip this block
   *
   * THRESHOLD STRATEGY:
   * - High threshold (>90): Triggers alert for excessive sensor readings
   * - Could indicate over-watering, sensor malfunction, or critical condition
   * - For plant monitoring: adjust threshold based on plant species needs
   */
  if (current_seconds > this->state_counter_) {
    this->state_counter_ = current_seconds;

    // Check high threshold: trigger error if value exceeds 90
    if (this->current_sensor_value_ > 90.0f) {
       ESP_LOGW(TAG, "High threshold exceeded! Sensor value: %.1f", this->current_sensor_value_);
       return &Controller::state_error;
    }
  }

  // =========================================================================
  // PHASE 2: Visual Logic - Breathing Animation
  // =========================================================================
  /**
   * Sine wave brightness modulation for "breathing" effect.
   *
   * MATH BREAKDOWN:
   * - t = millis() / 1000.0f: Convert ms to seconds (with decimals)
   * - sin(t * π): Sine wave with ~1 second period (2π radians)
   * - Range of sin(): -1.0 to +1.0
   * - (sin() + 1.0) / 2.0: Shift and scale to 0.0 to 1.0
   * - 0.1 + (brightness * 0.9): Remap to 10% to 100%
   *
   * WHY NOT 0% MINIMUM:
   * - 0% brightness = LED completely off
   * - Could be mistaken for system hang or power loss
   * - 10% minimum ensures LED is always visible
   * - Still creates strong breathing effect (10x brightness variation)
   *
   * WHY SINE WAVE (vs linear ramp or triangle wave):
   * - Sine accelerates smoothly (no sharp transitions)
   * - Natural-looking "breathing" motion
   * - Matches biological breathing rhythm (smooth acceleration/deceleration)
   *
   * WHY π (3.14159):
   * - Period of sin(t * π) is 2 seconds (full cycle: 0→π→2π)
   * - Creates slow, calming breathing effect
   * - Using π*2 would make it twice as fast (more agitated feeling)
   *
   * PERFORMANCE NOTE:
   * - sin() is computed every loop iteration (~1000 Hz)
   * - ESP32 has hardware floating-point, so this is fast enough
   * - Alternative: Lookup table would be faster but uses more memory
   */
  float t = millis() / 1000.0f;
  float brightness = (std::sin(t * 3.14159f) + 1.0f) / 2.0f; // 0.0 to 1.0
  brightness = 0.1f + (brightness * 0.9f); // Remap to 10% to 100%

  apply_light(0.0, 1.0, 0.0, brightness);

  // Stay in READY state (unless random error triggered above)
  return &Controller::state_ready;
}

// ============================================================================
// STATE: ERROR - Fault Condition Alert
// ============================================================================
/**
 * Fast red flashing for 5 seconds, then restart from INIT.
 *
 * PURPOSE:
 * - Visual alert for fault conditions (simulated via random trigger)
 * - In production, this would be triggered by:
 *   * Sensor reading out of range
 *   * Communication timeout
 *   * Calibration failure
 *   * Low battery or power issues
 * - Distinctive pattern (fast flashing) is attention-grabbing
 *
 * VISUAL: Fast Red Flashing
 * - Color: Pure red (r=1.0, g=0.0, b=0.0)
 * - Pattern: 100ms ON / 100ms OFF = 5 Hz
 * - Duration: 5 seconds total (25 flashes)
 *
 * WHY FAST FLASHING (5 Hz vs slower rates):
 * - URGENCY: Fast flashing universally signals "error" or "danger"
 * - ATTENTION: More likely to be noticed in peripheral vision
 * - DISTINCTION: Clearly different from calibration blink (1 Hz)
 * - Not too fast: 5 Hz is fast but not seizure-inducing (>10 Hz is risky)
 *
 * WHY 5 SECONDS:
 * - Long enough to be noticed if user isn't watching
 * - Long enough for user to investigate and check logs
 * - Short enough to not be eternally annoying
 * - Provides time for potential manual intervention before restart
 *
 * WHY RETURN TO INIT (not CALIBRATION or READY):
 * - Full restart provides cleanest recovery path
 * - Allows sensors to re-calibrate after fault
 * - User sees boot sequence again (confirmation of restart)
 * - In production, this could trigger deeper diagnostics
 */
Controller::StateFunc Controller::state_error() {
  uint32_t elapsed = millis() - this->state_start_time_;

  // After 5 seconds in ERROR state, perform full restart
  if (elapsed > 5000) {
    ESP_LOGI(TAG, "Error cleared. Re-initializing...");
    return &Controller::state_init; // Full restart from INIT
  }

  /**
   * Fast flashing pattern at 5 Hz.
   *
   * FLASHING MATH:
   * - elapsed / 100: Number of 100ms intervals passed
   * - % 2: Alternates between 0 (even) and 1 (odd)
   * - even = ON, odd = OFF
   * - Results in 5 Hz flashing (10 state changes per second)
   */
  bool on = (elapsed / 100) % 2 == 0;
  if (on) apply_light(1.0, 0.0, 0.0, 1.0);  // Red at full brightness
  else    apply_light(0.0, 0.0, 0.0, 0.0);  // Off

  // Stay in ERROR state until timeout
  return &Controller::state_error;
}

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

} // namespace controller
} // namespace esphome
