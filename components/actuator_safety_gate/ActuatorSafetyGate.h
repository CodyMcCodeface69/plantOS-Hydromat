#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <string>
#include <map>
#include <vector>

// Forward declaration for HAL
namespace plantos_hal {
class HAL;
}

namespace esphome {
namespace actuator_safety_gate {

/**
 * RampState - Tracks the current ramping state of an actuator
 */
enum RampState {
    RAMP_OFF,         // Actuator fully off (0% PWM duty cycle)
    RAMP_STARTING,    // Ramping up from 0% to 100%
    RAMP_FULL_ON,     // Fully on (100% PWM duty cycle)
    RAMP_STOPPING     // Ramping down from 100% to 0%
};

/**
 * ActuatorState - Tracks the state and timing information for a single actuator
 */
struct ActuatorState {
    bool lastRequestedState;      // Last state requested (true=ON, false=OFF)
    uint32_t lastCommandTime;     // Timestamp of last command (millis())
    uint32_t turnOnTime;          // Timestamp when actuator was turned ON (0 if OFF)
    uint32_t maxDuration;         // Maximum allowed duration in milliseconds (0 = no limit)

    // Soft-start/soft-stop fields
    uint32_t rampDuration;        // Ramp duration in milliseconds (0 = instant, no ramping)
    RampState rampState;          // Current ramping state
    uint32_t rampStartTime;       // Timestamp when current ramp started (millis())
    float currentDutyCycle;       // Current PWM duty cycle (0.0 = off, 1.0 = full on)

    // Duration violation tracking
    bool violationLogged;         // True if duration violation has been logged (prevents spam)

    // Intermittent cycling fields (for AirPump automatic ON/OFF cycling)
    bool cyclingEnabled;          // True if intermittent cycling is enabled
    uint32_t cyclingOnPeriod;     // ON period duration in milliseconds (0 = disabled)
    uint32_t cyclingOffPeriod;    // OFF period duration in milliseconds (0 = disabled)
    uint32_t cyclingLastToggle;   // Timestamp of last cycling toggle (millis())
    bool cyclingCurrentState;     // Current cycling state (true=ON, false=OFF)

    // State sync tracking (for Shelly HTTP-based actuators)
    uint32_t lastStateSyncTime;   // Timestamp of last updateActualState() call (0 = never synced)

    ActuatorState()
        : lastRequestedState(false),
          lastCommandTime(0),
          turnOnTime(0),
          maxDuration(0),
          rampDuration(0),
          rampState(RAMP_OFF),
          rampStartTime(0),
          currentDutyCycle(0.0f),
          violationLogged(false),
          cyclingEnabled(false),
          cyclingOnPeriod(0),
          cyclingOffPeriod(0),
          cyclingLastToggle(0),
          cyclingCurrentState(false),
          lastStateSyncTime(0) {}
};

/**
 * ActuatorSafetyGate - Centralized Gateway for Actuator Control
 *
 * ============================================================================
 * PURPOSE
 * ============================================================================
 *
 * This class serves as a CENTRALIZED GATEWAY for controlling all system
 * actuators, enforcing safety rules, preventing unnecessary operations, and
 * logging violations before execution.
 *
 * ============================================================================
 * KEY FEATURES
 * ============================================================================
 *
 * 1. DEBOUNCING: Prevents redundant commands by tracking the last requested
 *    state for each actuator. If the same state is requested again, the
 *    command is silently rejected to prevent unnecessary pin toggles.
 *
 * 2. MAXIMUM DURATION ENFORCEMENT: For critical actuators (pumps, valves),
 *    enforces maximum runtime limits to prevent overruns, spills, and
 *    hardware damage.
 *
 * 3. SOFT-START/SOFT-STOP: Gradually ramps PWM duty cycle from 0% to 100%
 *    (soft-start) or 100% to 0% (soft-stop) over a configurable duration.
 *    Protects circuits from inrush current and back-EMF spikes when controlling
 *    pumps, motors, and other inductive loads via MOSFETs.
 *
 * 4. SAFETY VIOLATION LOGGING: Every rejection is logged with clear messages
 *    indicating the reason (debouncing, duration limit, etc.).
 *
 * 5. RUNTIME TRACKING: Monitors how long actuators have been running and
 *    can detect if they exceed their configured limits.
 *
 * 6. CENTRALIZED CONTROL: Single point of control for all actuators,
 *    making it easy to add global safety rules or emergency shutoff.
 *
 * ============================================================================
 * USAGE EXAMPLE
 * ============================================================================
 *
 * ActuatorSafetyGate safetyGate;
 *
 * // Initialize the gate
 * safetyGate.begin();
 *
 * // Configure max duration for critical actuators
 * safetyGate.setMaxDuration("AcidPump", 10);  // 10 seconds max
 *
 * // Execute commands with safety enforcement
 * if (safetyGate.executeCommand("AcidPump", true, 5)) {
 *     // Command approved - turn on acid pump for max 5 seconds
 *     digitalWrite(ACID_PUMP_PIN, HIGH);
 * } else {
 *     // Command rejected - safety violation logged automatically
 * }
 *
 * // In loop(), check for auto-shutoff
 * safetyGate.loop();
 *
 * ============================================================================
 * SAFETY RULES
 * ============================================================================
 *
 * 1. Debouncing: Requesting the same state twice is rejected
 * 2. Max Duration: ON commands exceeding configured limits are rejected
 * 3. Runtime Monitoring: Actuators running beyond limits are flagged
 * 4. State Consistency: All state changes are tracked and validated
 */
class ActuatorSafetyGate : public Component {
public:
    ActuatorSafetyGate();

    /**
     * ESPHome Component setup() - Initialize the safety gate
     *
     * Called automatically by ESPHome during component initialization.
     */
    void setup() override;

    /**
     * ESPHome Component loop() - Periodic monitoring and violation detection
     *
     * Called continuously by ESPHome's main event loop.
     * Monitors actuator runtimes and logs duration violations.
     */
    void loop() override;

    // ========================================================================
    // DEPENDENCY INJECTION (Phase 2: HAL Integration)
    // ========================================================================

    /**
     * Set Hardware Abstraction Layer (HAL) dependency
     *
     * @param hal Pointer to PlantOS HAL instance
     *
     * This must be called during component initialization (via Python to_code).
     * The SafetyGate will use the HAL to execute approved actuator commands.
     */
    void setHAL(plantos_hal::HAL* hal) { hal_ = hal; }

    // ========================================================================
    // COMMAND EXECUTION
    // ========================================================================

    /**
     * Execute an actuator command with safety enforcement
     *
     * @param actuatorID Unique identifier for the actuator (e.g., "AcidPump", "WaterValve")
     * @param targetState Desired state (true=ON, false=OFF)
     * @param maxDurationSeconds Optional maximum duration for this command (seconds, 0=no limit)
     * @param forceExecute If true, bypass debouncing check (for health monitoring)
     * @return true if command is approved and should be executed, false if rejected
     *
     * SAFETY CHECKS PERFORMED:
     * 1. Debouncing: Rejects if targetState matches last requested state (bypassed if forceExecute=true)
     * 2. Max Duration: Rejects if maxDurationSeconds exceeds configured limit
     * 3. State Tracking: Updates internal state on approval
     *
     * EXAMPLES:
     *
     * // Turn on pump for max 5 seconds
     * if (executeCommand("AcidPump", true, 5)) {
     *     // Approved - turn on pump
     * }
     *
     * // Turn off pump
     * if (executeCommand("AcidPump", false)) {
     *     // Approved - turn off pump
     * }
     *
     * // Requesting same state again (rejected by debouncing)
     * executeCommand("AcidPump", true, 5);  // Approved
     * executeCommand("AcidPump", true, 5);  // REJECTED (debouncing)
     *
     * // Force execute - bypasses debouncing for health monitoring
     * executeCommand("AcidPump", true, 5, true);  // Approved (force execute)
     * executeCommand("AcidPump", true, 5, true);  // Also approved (debouncing bypassed)
     */
    bool executeCommand(const char* actuatorID, bool targetState, int maxDurationSeconds = 0, bool forceExecute = false);

    /**
     * Set maximum allowed duration for an actuator
     *
     * @param actuatorID Unique identifier for the actuator
     * @param maxSeconds Maximum runtime in seconds (0 = no limit)
     *
     * This sets a GLOBAL limit for the actuator. Individual commands can request
     * shorter durations, but cannot exceed this limit.
     *
     * EXAMPLE:
     * setMaxDuration("AcidPump", 30);  // Never allow pump to run > 30 seconds
     */
    void setMaxDuration(const char* actuatorID, int maxSeconds);

    /**
     * Enable/disable intermittent cycling for an actuator
     *
     * @param actuatorID Unique identifier for the actuator
     * @param enabled true to enable cycling, false to disable
     *
     * When enabled, the actuator will automatically cycle between ON and OFF
     * states based on the configured on/off periods.
     *
     * EXAMPLE:
     * enableCycling("AirPump", true);  // Enable automatic cycling
     */
    void enableCycling(const char* actuatorID, bool enabled);

    /**
     * Set cycling periods for an actuator
     *
     * @param actuatorID Unique identifier for the actuator
     * @param onPeriodSec ON period duration in seconds
     * @param offPeriodSec OFF period duration in seconds
     *
     * Configures the ON/OFF periods for intermittent cycling.
     * Cycling must be enabled separately via enableCycling().
     *
     * EXAMPLE:
     * setCyclingPeriods("AirPump", 120, 60);  // 2 min ON, 1 min OFF
     */
    void setCyclingPeriods(const char* actuatorID, uint32_t onPeriodSec, uint32_t offPeriodSec);

    /**
     * Get the current tracked state of an actuator
     *
     * @param actuatorID Unique identifier for the actuator
     * @return Current tracked state (true=ON, false=OFF)
     *
     * This returns the last APPROVED state, not necessarily the physical state.
     */
    bool getState(const char* actuatorID) const;

    /**
     * Update the actual state of an actuator from external polling
     *
     * This is called when the actual device state is polled (e.g., from Shelly HTTP)
     * and is used to keep debouncing in sync with the real device state.
     * If the actual state differs from the tracked state, the tracked state is updated.
     *
     * @param actuatorID Unique identifier for the actuator
     * @param actualState The actual state of the device (true=ON, false=OFF)
     *
     * EXAMPLE:
     * // Called from YAML when Shelly state is polled
     * updateActualState("AirPump", shellyState);  // Sync ASG with actual Shelly state
     */
    void updateActualState(const char* actuatorID, bool actualState);

    /**
     * Check if cycling is enabled for an actuator
     *
     * @param actuatorID Unique identifier for the actuator
     * @return true if intermittent cycling is enabled, false otherwise
     *
     * This allows external code to check if an actuator is in automatic cycling mode.
     */
    bool isCyclingEnabled(const char* actuatorID) const;

    // ========================================================================
    // SHELLY PATTERN API VALIDATION
    // ========================================================================

    /**
     * Validate an AirPump pattern against safety limits
     *
     * @param pattern Vector of durations in seconds [on, off, on, off, ...]
     * @return true if all ON durations are within max duration limit
     *
     * Checks each ON duration (positions 0, 2, 4, ...) against the configured
     * maximum duration for AirPump. If no max is configured (0), all patterns
     * are considered valid.
     */
    bool validateAirPumpPattern(const std::vector<uint32_t>& pattern) const;

    /**
     * Send a pattern sequence to AirPump via Shelly
     *
     * @param pattern Vector of durations in seconds [on, off, on, off, ...]
     * @param finalState State to set after pattern completes (true=ON, false=OFF)
     * @return true if pattern was validated and sent, false otherwise
     *
     * This method validates the pattern and sends it via HAL.
     * Controller should use this instead of calling HAL directly.
     */
    bool setAirPumpPattern(const std::vector<uint32_t>& pattern, bool finalState);

    /**
     * Stop any running AirPump sequence and set final state
     *
     * @param finalState State to set after stopping (true=ON, false=OFF)
     * @return true if command was sent, false if debounced (already in desired state)
     *
     * Includes debouncing: if we have valid state from polling and the current
     * state already matches finalState, the HTTP call is skipped.
     * Controller should use this instead of calling HAL directly.
     */
    bool stopAirPumpSequence(bool finalState);

    /**
     * Get the current runtime of an actuator (in seconds)
     *
     * @param actuatorID Unique identifier for the actuator
     * @return Runtime in seconds (0 if OFF or unknown)
     *
     * Returns how long the actuator has been ON since the last turn-on command.
     */
    uint32_t getRuntime(const char* actuatorID) const;

    /**
     * Check if an actuator is currently exceeding its maximum duration
     *
     * @param actuatorID Unique identifier for the actuator
     * @return true if actuator is ON and exceeding its max duration
     */
    bool isViolatingDuration(const char* actuatorID) const;

    /**
     * Force reset an actuator's state (for emergency/manual override)
     *
     * @param actuatorID Unique identifier for the actuator
     * @param newState New state to force (true=ON, false=OFF)
     *
     * WARNING: This bypasses all safety checks. Use only for emergency
     * shutoff or manual override after physical verification.
     */
    void forceReset(const char* actuatorID, bool newState);

    /**
     * Get statistics for an actuator
     *
     * @param actuatorID Unique identifier for the actuator
     * @param outLastState Output: Last requested state
     * @param outRuntimeSeconds Output: Current runtime in seconds
     * @param outMaxDurationSeconds Output: Configured max duration
     * @return true if actuator exists in tracking map, false otherwise
     */
    bool getStats(const char* actuatorID,
                  bool& outLastState,
                  uint32_t& outRuntimeSeconds,
                  uint32_t& outMaxDurationSeconds) const;

    // ========================================================================
    // DURATION QUERY API (for enhanced error handling)
    // ========================================================================

    /**
     * Query the configured maximum duration for an actuator
     *
     * @param actuatorID Unique identifier for the actuator
     * @return Maximum duration in seconds (0 if no limit configured)
     *
     * This allows the controller to query what the maximum allowed duration is
     * before making a request, enabling adaptive duration adjustment.
     *
     * EXAMPLE:
     * uint32_t max_sec = safetyGate.getMaxDurationSeconds("AcidPump");
     * // Returns 30 if max configured as 30s, or 0 if no limit
     */
    uint32_t getMaxDurationSeconds(const char* actuatorID) const;

    /**
     * Calculate nearest legal duration within safety limits
     *
     * @param actuatorID Unique identifier for the actuator
     * @param requested_duration_sec Requested duration in seconds
     * @return Adapted duration that fits within max limit (0 if cannot adapt)
     *
     * If requested duration exceeds configured max, returns the max duration.
     * If no max is configured, returns requested duration unchanged.
     * Returns 0 if actuator doesn't exist or has 0 max (infinite).
     *
     * CRITICAL: This is how controller adapts to duration violations!
     *
     * EXAMPLE:
     * uint32_t adapted = safetyGate->getAdaptedDuration("AcidPump", 45);
     * // Returns 30 if max is 30 seconds
     * // Returns 45 if max is 0 (no limit) or max >= 45
     */
    uint32_t getAdaptedDuration(const char* actuatorID, uint32_t requested_duration_sec) const;

    // ========================================================================
    // SOFT-START / SOFT-STOP METHODS
    // ========================================================================

    /**
     * Set ramp duration for soft-start/soft-stop of an actuator
     *
     * @param actuatorID Unique identifier for the actuator
     * @param rampMs Ramp duration in milliseconds (0 = instant on/off, no ramping)
     *
     * When ramping is enabled, the actuator will gradually transition between
     * off (0% PWM duty cycle) and on (100% PWM duty cycle) over the specified
     * duration. This protects circuits from inrush current and back-EMF spikes.
     *
     * EXAMPLE:
     * setRampDuration("AcidPump", 2000);  // 2-second soft-start/soft-stop
     */
    void setRampDuration(const char* actuatorID, uint32_t rampMs);

    /**
     * Get current PWM duty cycle for an actuator
     *
     * @param actuatorID Unique identifier for the actuator
     * @return Current duty cycle (0.0 = off, 1.0 = full on)
     *
     * During ramping, this value will gradually change from 0.0 to 1.0
     * (soft-start) or from 1.0 to 0.0 (soft-stop). Apply this value to
     * your PWM output to control MOSFET gate voltage.
     *
     * EXAMPLE:
     * float duty = safetyGate.getDutyCycle("AcidPump");
     * ledcWrite(PWM_CHANNEL, duty * 255);  // Apply to ESP32 LEDC PWM
     */
    float getDutyCycle(const char* actuatorID) const;

    /**
     * Check if an actuator is currently ramping (soft-start or soft-stop)
     *
     * @param actuatorID Unique identifier for the actuator
     * @return true if actuator is currently ramping, false otherwise
     *
     * Returns true if the actuator is in RAMP_STARTING or RAMP_STOPPING state.
     * Useful for determining when ramping is complete.
     */
    bool isRamping(const char* actuatorID) const;

    /**
     * Get the current ramp state of an actuator
     *
     * @param actuatorID Unique identifier for the actuator
     * @return Current RampState (RAMP_OFF, RAMP_STARTING, RAMP_FULL_ON, RAMP_STOPPING)
     */
    RampState getRampState(const char* actuatorID) const;

    // ========================================================================
    // SAFETY GATE ENABLE/DISABLE
    // ========================================================================

    /**
     * Enable or disable the safety gate
     *
     * @param enabled true to enable safety checks, false to disable
     *
     * When disabled, the safety gate will still execute commands and track
     * state, but will NOT enforce duration limits or auto-shutoff.
     * This is useful for maintenance, testing, or pump flushing operations.
     *
     * WARNING: Disabling safety gate removes all protection against runaway
     * actuators. Only disable when you have manual oversight.
     */
    void setEnabled(bool enabled);

    /**
     * Check if safety gate is currently enabled
     *
     * @return true if safety checks are active, false if disabled
     */
    bool isEnabled() const { return enabled_; }

private:
    // ========================================================================
    // DEPENDENCIES (Phase 2: HAL Integration)
    // ========================================================================

    // Hardware Abstraction Layer for actuator control
    plantos_hal::HAL* hal_{nullptr};

    // ========================================================================
    // SAFETY GATE STATE
    // ========================================================================

    // Safety gate enabled/disabled flag (enabled by default)
    bool enabled_{true};

    // ========================================================================
    // STATE TRACKING
    // ========================================================================

    // Map of actuator ID to state tracking info
    std::map<std::string, ActuatorState> actuators_;

    // ========================================================================
    // SHELLY AIRPUMP CYCLING STATE
    // ========================================================================

    // Track if Shelly cycling pattern is active (to avoid redundant calls)
    bool shelly_cycling_active_{false};

    // Configured cycling periods (in seconds) for pattern generation
    uint32_t shelly_cycling_on_period_{0};
    uint32_t shelly_cycling_off_period_{0};

    // Timestamp of last Shelly pattern refresh
    uint32_t last_shelly_refresh_{0};

    // ========================================================================
    // HELPER METHODS
    // ========================================================================

    // Helper to get or create actuator state entry
    ActuatorState& getOrCreateState(const std::string& actuatorID);

    // Helper to execute hardware command via HAL
    void executeHardwareCommand(const std::string& actuatorID, bool state);

    // Helper to log safety violations
    void logRejection(const char* actuatorID, const char* reason);

    // Helper to log command approval
    void logApproval(const char* actuatorID, bool targetState, int maxDurationSeconds);

    // Helper to update ramping state and duty cycle
    void updateRamping(const std::string& actuatorID, ActuatorState& state, uint32_t currentTime);
};

} // namespace actuator_safety_gate
} // namespace esphome
