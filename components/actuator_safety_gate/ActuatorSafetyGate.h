#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <string>
#include <map>

namespace esphome {
namespace actuator_safety_gate {

/**
 * ActuatorState - Tracks the state and timing information for a single actuator
 */
struct ActuatorState {
    bool lastRequestedState;      // Last state requested (true=ON, false=OFF)
    uint32_t lastCommandTime;     // Timestamp of last command (millis())
    uint32_t turnOnTime;          // Timestamp when actuator was turned ON (0 if OFF)
    uint32_t maxDuration;         // Maximum allowed duration in milliseconds (0 = no limit)

    ActuatorState()
        : lastRequestedState(false),
          lastCommandTime(0),
          turnOnTime(0),
          maxDuration(0) {}
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
 * 3. SAFETY VIOLATION LOGGING: Every rejection is logged with clear messages
 *    indicating the reason (debouncing, duration limit, etc.).
 *
 * 4. RUNTIME TRACKING: Monitors how long actuators have been running and
 *    can detect if they exceed their configured limits.
 *
 * 5. CENTRALIZED CONTROL: Single point of control for all actuators,
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

    /**
     * Execute an actuator command with safety enforcement
     *
     * @param actuatorID Unique identifier for the actuator (e.g., "AcidPump", "WaterValve")
     * @param targetState Desired state (true=ON, false=OFF)
     * @param maxDurationSeconds Optional maximum duration for this command (seconds, 0=no limit)
     * @return true if command is approved and should be executed, false if rejected
     *
     * SAFETY CHECKS PERFORMED:
     * 1. Debouncing: Rejects if targetState matches last requested state
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
     * // Requesting same state again (rejected)
     * executeCommand("AcidPump", true, 5);  // Approved
     * executeCommand("AcidPump", true, 5);  // REJECTED (debouncing)
     */
    bool executeCommand(const char* actuatorID, bool targetState, int maxDurationSeconds = 0);

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
     * Get the current tracked state of an actuator
     *
     * @param actuatorID Unique identifier for the actuator
     * @return Current tracked state (true=ON, false=OFF)
     *
     * This returns the last APPROVED state, not necessarily the physical state.
     */
    bool getState(const char* actuatorID) const;

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

private:
    // Map of actuator ID to state tracking info
    std::map<std::string, ActuatorState> actuators_;

    // Helper to get or create actuator state entry
    ActuatorState& getOrCreateState(const std::string& actuatorID);

    // Helper to log safety violations
    void logRejection(const char* actuatorID, const char* reason);

    // Helper to log command approval
    void logApproval(const char* actuatorID, bool targetState, int maxDurationSeconds);
};

} // namespace actuator_safety_gate
} // namespace esphome
