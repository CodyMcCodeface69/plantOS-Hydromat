#include "ActuatorSafetyGate.h"
#include <cstring>

namespace esphome {
namespace actuator_safety_gate {

static const char *TAG = "actuator.safety";

ActuatorSafetyGate::ActuatorSafetyGate() {
}

void ActuatorSafetyGate::setup() {
    ESP_LOGI(TAG, "ActuatorSafetyGate initialized");
    ESP_LOGI(TAG, "Safety features: Debouncing, Duration Limits, Runtime Tracking, Soft-Start/Soft-Stop");
}

bool ActuatorSafetyGate::executeCommand(const char* actuatorID,
                                        bool targetState,
                                        int maxDurationSeconds) {
    // Input validation
    if (actuatorID == nullptr || strlen(actuatorID) == 0) {
        ESP_LOGE(TAG, "REJECTED: Invalid actuator ID (null or empty)");
        return false;
    }

    std::string id(actuatorID);
    ActuatorState& state = getOrCreateState(id);
    uint32_t currentTime = esphome::millis();

    // ========================================================================
    // SAFETY CHECK 1: DEBOUNCING
    // ========================================================================
    // Reject if requesting the same state as currently tracked
    // This prevents unnecessary pin toggles and redundant operations
    if (state.lastRequestedState == targetState) {
        logRejection(actuatorID, "Debouncing - state already requested");
        return false;
    }

    // ========================================================================
    // SAFETY CHECK 2: MAXIMUM DURATION ENFORCEMENT
    // ========================================================================
    // Only check duration limits for ON commands with specified duration
    if (targetState == true && maxDurationSeconds > 0) {
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
        } else {
            // Instant ON (no ramping)
            state.rampState = RAMP_FULL_ON;
            state.currentDutyCycle = 1.0f;
            logApproval(actuatorID, targetState, maxDurationSeconds);
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
        } else {
            // Instant OFF (no ramping or already off)
            state.rampState = RAMP_OFF;
            state.currentDutyCycle = 0.0f;
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

void ActuatorSafetyGate::loop() {
    uint32_t currentTime = esphome::millis();

    for (auto& pair : actuators_) {
        const std::string& id = pair.first;
        ActuatorState& state = pair.second;

        // Update ramping state and duty cycle for this actuator
        updateRamping(id, state, currentTime);

        // Check if actuator is ON and has a max duration configured
        if (state.lastRequestedState == true &&
            state.turnOnTime > 0 &&
            state.maxDuration > 0) {

            uint32_t runtime = currentTime - state.turnOnTime;

            // Check if runtime exceeds maximum duration
            if (runtime > state.maxDuration) {
                uint32_t runtimeSeconds = runtime / 1000;
                uint32_t maxSeconds = state.maxDuration / 1000;

                ESP_LOGW(TAG, "DURATION VIOLATION: %s has been ON for %u seconds (limit: %u seconds)",
                         id.c_str(), runtimeSeconds, maxSeconds);
                ESP_LOGW(TAG, "  -> Manual intervention required or automatic shutoff should be triggered!");
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

} // namespace actuator_safety_gate
} // namespace esphome
