#include "ActuatorSafetyGate.h"
#include "esphome/components/plantos_hal/hal.h"
#include <cstring>

namespace esphome {
namespace actuator_safety_gate {

static const char *TAG = "actuator.safety";

ActuatorSafetyGate::ActuatorSafetyGate() {
}

void ActuatorSafetyGate::setup() {
    ESP_LOGI(TAG, "ActuatorSafetyGate initialized");
    ESP_LOGI(TAG, "Safety features: Debouncing, Duration Limits, Runtime Tracking, Soft-Start/Soft-Stop");
    ESP_LOGI(TAG, "Safety gate status: ENABLED (default)");

    // Verify HAL dependency (Phase 2)
    if (!hal_) {
        ESP_LOGW(TAG, "HAL not configured - actuator commands will be logged but not executed");
    } else {
        ESP_LOGI(TAG, "HAL configured - actuators will be controlled via hardware abstraction layer");
    }
}

void ActuatorSafetyGate::setEnabled(bool enabled) {
    enabled_ = enabled;
    if (enabled) {
        ESP_LOGI(TAG, "================================================================================");
        ESP_LOGI(TAG, "Safety Gate: ENABLED");
        ESP_LOGI(TAG, "Duration limits and auto-shutoff are now ACTIVE");
        ESP_LOGI(TAG, "================================================================================");
    } else {
        ESP_LOGW(TAG, "================================================================================");
        ESP_LOGW(TAG, "Safety Gate: DISABLED");
        ESP_LOGW(TAG, "WARNING: Duration limits and auto-shutoff are now INACTIVE");
        ESP_LOGW(TAG, "Manual oversight required to prevent runaway actuators!");
        ESP_LOGW(TAG, "================================================================================");
    }
}

bool ActuatorSafetyGate::executeCommand(const char* actuatorID,
                                        bool targetState,
                                        int maxDurationSeconds,
                                        bool forceExecute) {
    // Input validation
    if (actuatorID == nullptr || strlen(actuatorID) == 0) {
        ESP_LOGE(TAG, "REJECTED: Invalid actuator ID (null or empty)");
        return false;
    }

    std::string id(actuatorID);
    ActuatorState& state = getOrCreateState(id);
    uint32_t currentTime = esphome::millis();

    // ========================================================================
    // SAFETY CHECK 1: DEBOUNCING (only if safety gate is enabled)
    // ========================================================================
    // Reject if requesting the same state as currently tracked
    // This prevents unnecessary pin toggles and redundant operations
    // NOTE: Debouncing is bypassed when safety gate is disabled OR when forceExecute=true
    if (enabled_ && !forceExecute && state.lastRequestedState == targetState) {
        logRejection(actuatorID, "Debouncing - state already requested");
        return false;
    }

    // Log when force execute bypasses debouncing
    if (forceExecute && state.lastRequestedState == targetState) {
        ESP_LOGD(TAG, "[FORCE EXECUTE] %s → %s (bypassing debouncing)",
                 actuatorID, targetState ? "ON" : "OFF");
    }

    // ========================================================================
    // SAFETY CHECK 2: MAXIMUM DURATION ENFORCEMENT (only if enabled)
    // ========================================================================
    // Only check duration limits for ON commands with specified duration
    if (enabled_ && targetState == true && maxDurationSeconds > 0) {
        // Convert to milliseconds for internal tracking
        uint32_t requestedDurationMs = maxDurationSeconds * 1000;

        // Check if this actuator has a configured maximum duration
        if (state.maxDuration > 0) {
            // Reject if requested duration exceeds configured limit
            if (requestedDurationMs > state.maxDuration) {
                char reason[128];
                snprintf(reason, sizeof(reason),
                         "Max duration violation - requested %ds exceeds limit %ds",
                         maxDurationSeconds,
                         state.maxDuration / 1000);
                logRejection(actuatorID, reason);
                return false;
            }
        }
    }

    // ========================================================================
    // COMMAND APPROVED - Update State Tracking
    // ========================================================================

    // Update state tracking
    state.lastRequestedState = targetState;
    state.lastCommandTime = currentTime;

    if (targetState == true) {
        // Turning ON
        state.turnOnTime = currentTime;

        // Check if ramping is configured
        if (state.rampDuration > 0) {
            // Start soft-start ramping
            state.rampState = RAMP_STARTING;
            state.rampStartTime = currentTime;
            state.currentDutyCycle = 0.0f;
            ESP_LOGI(TAG, "APPROVED: %s RAMPING UP (duration: %u ms)",
                     actuatorID, state.rampDuration);
            // Hardware control will be handled gradually in updateRamping()
        } else {
            // Instant ON (no ramping)
            state.rampState = RAMP_FULL_ON;
            state.currentDutyCycle = 1.0f;
            logApproval(actuatorID, targetState, maxDurationSeconds);

            // Execute hardware command via HAL (Phase 2)
            executeHardwareCommand(id, true);
        }
    } else {
        // Turning OFF - calculate total runtime
        if (state.turnOnTime > 0) {
            uint32_t runtime = (currentTime - state.turnOnTime) / 1000;
            ESP_LOGI(TAG, "APPROVED: %s OFF (ran for %u seconds)",
                     actuatorID, runtime);
        } else {
            ESP_LOGI(TAG, "APPROVED: %s OFF", actuatorID);
        }

        // Check if ramping is configured
        if (state.rampDuration > 0 && state.currentDutyCycle > 0.0f) {
            // Start soft-stop ramping from current duty cycle
            state.rampState = RAMP_STOPPING;
            state.rampStartTime = currentTime;
            ESP_LOGI(TAG, "         %s RAMPING DOWN (duration: %u ms)",
                     actuatorID, state.rampDuration);
            // Hardware control will be handled gradually in updateRamping()
        } else {
            // Instant OFF (no ramping or already off)
            state.rampState = RAMP_OFF;
            state.currentDutyCycle = 0.0f;

            // Execute hardware command via HAL (Phase 2)
            executeHardwareCommand(id, false);
        }

        // Clear turn-on time
        state.turnOnTime = 0;
    }

    return true;
}

void ActuatorSafetyGate::setMaxDuration(const char* actuatorID, int maxSeconds) {
    if (actuatorID == nullptr || strlen(actuatorID) == 0) {
        ESP_LOGE(TAG, "Cannot set max duration: Invalid actuator ID");
        return;
    }

    std::string id(actuatorID);
    ActuatorState& state = getOrCreateState(id);

    // Convert seconds to milliseconds
    state.maxDuration = maxSeconds * 1000;

    if (maxSeconds > 0) {
        ESP_LOGI(TAG, "Max duration set: %s = %d seconds", actuatorID, maxSeconds);
    } else {
        ESP_LOGI(TAG, "Max duration removed: %s (no limit)", actuatorID);
    }
}

void ActuatorSafetyGate::enableCycling(const char* actuatorID, bool enabled) {
    if (actuatorID == nullptr || strlen(actuatorID) == 0) {
        ESP_LOGE(TAG, "Cannot enable cycling: Invalid actuator ID");
        return;
    }

    std::string id(actuatorID);
    ActuatorState& state = getOrCreateState(id);

    state.cyclingEnabled = enabled;

    if (enabled) {
        ESP_LOGI(TAG, "Intermittent cycling ENABLED for %s (ON: %us, OFF: %us)",
                 actuatorID,
                 state.cyclingOnPeriod / 1000,
                 state.cyclingOffPeriod / 1000);
        // Initialize cycling state - START WITH ON CYCLE
        state.cyclingCurrentState = true;  // Start with ON
        state.cyclingLastToggle = esphome::millis();

        // Immediately turn ON the actuator to start the ON cycle
        ESP_LOGI(TAG, "Starting %s with ON cycle", actuatorID);
        executeCommand(actuatorID, true, state.cyclingOnPeriod / 1000);
    } else {
        ESP_LOGI(TAG, "Intermittent cycling DISABLED for %s", actuatorID);
        // Turn off the actuator when cycling is disabled
        // NOTE: Controller's Normal mode health check will turn pump back ON within 30s
        executeCommand(actuatorID, false);
    }
}

void ActuatorSafetyGate::setCyclingPeriods(const char* actuatorID, uint32_t onPeriodSec, uint32_t offPeriodSec) {
    if (actuatorID == nullptr || strlen(actuatorID) == 0) {
        ESP_LOGE(TAG, "Cannot set cycling periods: Invalid actuator ID");
        return;
    }

    std::string id(actuatorID);
    ActuatorState& state = getOrCreateState(id);

    // Convert seconds to milliseconds
    state.cyclingOnPeriod = onPeriodSec * 1000;
    state.cyclingOffPeriod = offPeriodSec * 1000;

    ESP_LOGI(TAG, "Cycling periods set for %s: ON=%us, OFF=%us",
             actuatorID, onPeriodSec, offPeriodSec);

    // If cycling is already enabled, log a reminder
    if (state.cyclingEnabled) {
        ESP_LOGI(TAG, "Note: Cycling is currently active for %s with new periods", actuatorID);
    }
}

void ActuatorSafetyGate::loop() {
    uint32_t currentTime = esphome::millis();

    for (auto& pair : actuators_) {
        const std::string& id = pair.first;
        ActuatorState& state = pair.second;

        // ====================================================================
        // INTERMITTENT CYCLING
        // ====================================================================
        // Check if intermittent cycling is enabled for this actuator
        if (state.cyclingEnabled &&
            state.cyclingOnPeriod > 0 &&
            state.cyclingOffPeriod > 0) {

            uint32_t elapsed = currentTime - state.cyclingLastToggle;
            uint32_t period = state.cyclingCurrentState ? state.cyclingOnPeriod : state.cyclingOffPeriod;

            // Time to toggle?
            if (elapsed >= period) {
                // Toggle cycling state
                state.cyclingCurrentState = !state.cyclingCurrentState;
                state.cyclingLastToggle = currentTime;

                // Execute the toggle via executeCommand
                // This respects all safety rules (max duration, debouncing, etc.)
                const char* id_cstr = id.c_str();
                uint32_t duration_sec = state.cyclingCurrentState ? (state.cyclingOnPeriod / 1000) : 0;

                ESP_LOGD(TAG, "[CYCLING] %s → %s (period elapsed: %u ms)",
                         id_cstr,
                         state.cyclingCurrentState ? "ON" : "OFF",
                         elapsed);

                executeCommand(id_cstr, state.cyclingCurrentState, duration_sec);
            }
        }

        // Update ramping state and duty cycle for this actuator
        updateRamping(id, state, currentTime);

        // Skip duration monitoring if safety gate is disabled
        if (!enabled_) {
            continue;
        }

        // Check if actuator is ON and has a max duration configured
        if (state.lastRequestedState == true &&
            state.turnOnTime > 0 &&
            state.maxDuration > 0) {

            uint32_t runtime = currentTime - state.turnOnTime;

            // Check if runtime exceeds maximum duration
            if (runtime > state.maxDuration) {
                uint32_t runtimeSeconds = runtime / 1000;
                uint32_t maxSeconds = state.maxDuration / 1000;

                // Only log once per violation to prevent spam
                if (!state.violationLogged) {
                    ESP_LOGE(TAG, "================================================================================");
                    ESP_LOGE(TAG, "CRITICAL: DURATION VIOLATION DETECTED!");
                    ESP_LOGE(TAG, "Actuator: %s has been ON for %u seconds (limit: %u seconds)",
                             id.c_str(), runtimeSeconds, maxSeconds);
                    ESP_LOGE(TAG, "Action: Forcing automatic shutoff NOW!");
                    ESP_LOGE(TAG, "================================================================================");
                    state.violationLogged = true;
                }

                // FORCE SHUTOFF: Turn off the actuator immediately
                // Bypass debouncing by directly updating state and executing hardware command
                state.lastRequestedState = false;
                state.turnOnTime = 0;
                state.rampState = RAMP_OFF;
                state.currentDutyCycle = 0.0f;

                // Execute hardware shutoff via HAL
                executeHardwareCommand(id, false);
            }
        } else {
            // Reset violation flag when actuator is turned off
            if (!state.lastRequestedState) {
                state.violationLogged = false;
            }
        }
    }
}

bool ActuatorSafetyGate::getState(const char* actuatorID) const {
    if (actuatorID == nullptr) return false;

    std::string id(actuatorID);
    auto it = actuators_.find(id);

    if (it != actuators_.end()) {
        return it->second.lastRequestedState;
    }

    return false; // Unknown actuator defaults to OFF
}

bool ActuatorSafetyGate::isCyclingEnabled(const char* actuatorID) const {
    if (actuatorID == nullptr) return false;

    std::string id(actuatorID);
    auto it = actuators_.find(id);

    if (it != actuators_.end()) {
        return it->second.cyclingEnabled;
    }

    return false; // Unknown actuator - no cycling
}

uint32_t ActuatorSafetyGate::getRuntime(const char* actuatorID) const {
    if (actuatorID == nullptr) return 0;

    std::string id(actuatorID);
    auto it = actuators_.find(id);

    if (it != actuators_.end()) {
        const ActuatorState& state = it->second;

        // Only calculate runtime if actuator is ON
        if (state.lastRequestedState && state.turnOnTime > 0) {
            uint32_t runtime = (esphome::millis() - state.turnOnTime) / 1000;
            return runtime;
        }
    }

    return 0;
}

bool ActuatorSafetyGate::isViolatingDuration(const char* actuatorID) const {
    if (actuatorID == nullptr) return false;

    std::string id(actuatorID);
    auto it = actuators_.find(id);

    if (it != actuators_.end()) {
        const ActuatorState& state = it->second;

        // Check if ON, has max duration, and exceeds it
        if (state.lastRequestedState &&
            state.turnOnTime > 0 &&
            state.maxDuration > 0) {

            uint32_t runtime = esphome::millis() - state.turnOnTime;
            return runtime > state.maxDuration;
        }
    }

    return false;
}

void ActuatorSafetyGate::forceReset(const char* actuatorID, bool newState) {
    if (actuatorID == nullptr) return;

    std::string id(actuatorID);
    ActuatorState& state = getOrCreateState(id);

    ESP_LOGW(TAG, "FORCE RESET: %s -> %s (bypassing safety checks)",
             actuatorID, newState ? "ON" : "OFF");

    state.lastRequestedState = newState;
    state.lastCommandTime = esphome::millis();

    if (newState) {
        state.turnOnTime = esphome::millis();
    } else {
        state.turnOnTime = 0;
    }
}

bool ActuatorSafetyGate::getStats(const char* actuatorID,
                                  bool& outLastState,
                                  uint32_t& outRuntimeSeconds,
                                  uint32_t& outMaxDurationSeconds) const {
    if (actuatorID == nullptr) return false;

    std::string id(actuatorID);
    auto it = actuators_.find(id);

    if (it != actuators_.end()) {
        const ActuatorState& state = it->second;

        outLastState = state.lastRequestedState;
        outMaxDurationSeconds = state.maxDuration / 1000;

        // Calculate runtime if ON
        if (state.lastRequestedState && state.turnOnTime > 0) {
            outRuntimeSeconds = (esphome::millis() - state.turnOnTime) / 1000;
        } else {
            outRuntimeSeconds = 0;
        }

        return true;
    }

    return false;
}

// ============================================================================
// SOFT-START / SOFT-STOP METHODS
// ============================================================================

void ActuatorSafetyGate::setRampDuration(const char* actuatorID, uint32_t rampMs) {
    if (actuatorID == nullptr || strlen(actuatorID) == 0) {
        ESP_LOGE(TAG, "Cannot set ramp duration: Invalid actuator ID");
        return;
    }

    std::string id(actuatorID);
    ActuatorState& state = getOrCreateState(id);

    state.rampDuration = rampMs;

    if (rampMs > 0) {
        ESP_LOGI(TAG, "Ramp duration set: %s = %u ms (soft-start/soft-stop enabled)",
                 actuatorID, rampMs);
    } else {
        ESP_LOGI(TAG, "Ramp duration removed: %s (instant on/off)", actuatorID);
    }
}

float ActuatorSafetyGate::getDutyCycle(const char* actuatorID) const {
    if (actuatorID == nullptr) return 0.0f;

    std::string id(actuatorID);
    auto it = actuators_.find(id);

    if (it != actuators_.end()) {
        return it->second.currentDutyCycle;
    }

    return 0.0f;  // Unknown actuator defaults to 0% duty cycle
}

bool ActuatorSafetyGate::isRamping(const char* actuatorID) const {
    if (actuatorID == nullptr) return false;

    std::string id(actuatorID);
    auto it = actuators_.find(id);

    if (it != actuators_.end()) {
        const ActuatorState& state = it->second;
        return (state.rampState == RAMP_STARTING || state.rampState == RAMP_STOPPING);
    }

    return false;
}

RampState ActuatorSafetyGate::getRampState(const char* actuatorID) const {
    if (actuatorID == nullptr) return RAMP_OFF;

    std::string id(actuatorID);
    auto it = actuators_.find(id);

    if (it != actuators_.end()) {
        return it->second.rampState;
    }

    return RAMP_OFF;  // Unknown actuator defaults to RAMP_OFF
}

// ============================================================================
// PRIVATE HELPER METHODS
// ============================================================================

ActuatorState& ActuatorSafetyGate::getOrCreateState(const std::string& actuatorID) {
    // If actuator doesn't exist in map, create it with default state
    auto it = actuators_.find(actuatorID);
    if (it == actuators_.end()) {
        // Create new entry
        actuators_[actuatorID] = ActuatorState();
        ESP_LOGD(TAG, "New actuator registered: %s", actuatorID.c_str());
    }

    return actuators_[actuatorID];
}

void ActuatorSafetyGate::logRejection(const char* actuatorID, const char* reason) {
    ESP_LOGW(TAG, "REJECTED: %s - %s", actuatorID, reason);
}

void ActuatorSafetyGate::logApproval(const char* actuatorID,
                                     bool targetState,
                                     int maxDurationSeconds) {
    if (targetState) {
        if (maxDurationSeconds > 0) {
            ESP_LOGI(TAG, "APPROVED: %s ON (max duration: %d seconds)",
                     actuatorID, maxDurationSeconds);
        } else {
            ESP_LOGI(TAG, "APPROVED: %s ON (no duration limit)", actuatorID);
        }
    } else {
        ESP_LOGI(TAG, "APPROVED: %s OFF", actuatorID);
    }
}

void ActuatorSafetyGate::updateRamping(const std::string& actuatorID,
                                       ActuatorState& state,
                                       uint32_t currentTime) {
    // Only process if actuator is in a ramping state
    if (state.rampState != RAMP_STARTING && state.rampState != RAMP_STOPPING) {
        return;
    }

    // Calculate elapsed time since ramp started
    uint32_t elapsed = currentTime - state.rampStartTime;

    // Safety check: ensure rampDuration is not zero (should never happen, but prevent division by zero)
    if (state.rampDuration == 0) {
        ESP_LOGW(TAG, "Ramp duration is 0 for %s in ramping state - forcing instant transition",
                 actuatorID.c_str());
        if (state.rampState == RAMP_STARTING) {
            state.rampState = RAMP_FULL_ON;
            state.currentDutyCycle = 1.0f;
        } else {
            state.rampState = RAMP_OFF;
            state.currentDutyCycle = 0.0f;
        }
        return;
    }

    if (state.rampState == RAMP_STARTING) {
        // ====================================================================
        // SOFT-START: Ramp UP from 0% to 100%
        // ====================================================================
        if (elapsed >= state.rampDuration) {
            // Ramp complete - transition to FULL_ON
            state.rampState = RAMP_FULL_ON;
            state.currentDutyCycle = 1.0f;
            ESP_LOGI(TAG, "Ramp complete: %s now at 100%% (FULL ON)", actuatorID.c_str());

            // Execute final hardware command via HAL (Phase 2)
            executeHardwareCommand(actuatorID, true);
        } else {
            // Calculate linear ramp: 0.0 to 1.0 over rampDuration
            float progress = static_cast<float>(elapsed) / static_cast<float>(state.rampDuration);
            state.currentDutyCycle = progress;

            // Clamp to valid range [0.0, 1.0] for safety
            if (state.currentDutyCycle > 1.0f) {
                state.currentDutyCycle = 1.0f;
            }
        }

    } else if (state.rampState == RAMP_STOPPING) {
        // ====================================================================
        // SOFT-STOP: Ramp DOWN from 100% to 0%
        // ====================================================================
        if (elapsed >= state.rampDuration) {
            // Ramp complete - transition to OFF
            state.rampState = RAMP_OFF;
            state.currentDutyCycle = 0.0f;
            ESP_LOGI(TAG, "Ramp complete: %s now at 0%% (OFF)", actuatorID.c_str());

            // Execute final hardware command via HAL (Phase 2)
            executeHardwareCommand(actuatorID, false);
        } else {
            // Calculate linear ramp: 1.0 to 0.0 over rampDuration
            float progress = static_cast<float>(elapsed) / static_cast<float>(state.rampDuration);
            state.currentDutyCycle = 1.0f - progress;

            // Clamp to valid range [0.0, 1.0] for safety
            if (state.currentDutyCycle < 0.0f) {
                state.currentDutyCycle = 0.0f;
            }
        }
    }
}

// ============================================================================
// HARDWARE EXECUTION (Phase 2: HAL Integration)
// ============================================================================

void ActuatorSafetyGate::executeHardwareCommand(const std::string& actuatorID, bool state) {
    // Check if HAL is available
    if (!hal_) {
        ESP_LOGD(TAG, "HAL not available - skipping hardware execution for %s", actuatorID.c_str());
        return;
    }

    // Determine actuator type and call appropriate HAL method
    // Convention: IDs ending in "Pump" are pumps, ending in "Valve" are valves
    if (actuatorID.find("Pump") != std::string::npos) {
        // This is a pump - call HAL setPump()
        ESP_LOGD(TAG, "Executing via HAL: setPump(%s, %s)",
                 actuatorID.c_str(), state ? "ON" : "OFF");
        hal_->setPump(actuatorID, state);
    }
    else if (actuatorID.find("Valve") != std::string::npos) {
        // This is a valve - call HAL setValve()
        ESP_LOGD(TAG, "Executing via HAL: setValve(%s, %s)",
                 actuatorID.c_str(), state ? "OPEN" : "CLOSED");
        hal_->setValve(actuatorID, state);
    }
    else {
        // Unknown actuator type - default to pump
        ESP_LOGW(TAG, "Unknown actuator type: %s (defaulting to pump)", actuatorID.c_str());
        hal_->setPump(actuatorID, state);
    }
}

} // namespace actuator_safety_gate
} // namespace esphome
