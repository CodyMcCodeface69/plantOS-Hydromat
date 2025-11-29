#include "ActuatorSafetyGate.h"
#include <cstring>

namespace esphome {
namespace actuator_safety_gate {

static const char *TAG = "actuator.safety";

ActuatorSafetyGate::ActuatorSafetyGate() {
}

void ActuatorSafetyGate::setup() {
    ESP_LOGI(TAG, "ActuatorSafetyGate initialized");
    ESP_LOGI(TAG, "Safety features: Debouncing, Duration Limits, Runtime Tracking");
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
        // Turning ON - record start time
        state.turnOnTime = currentTime;

        // Log approval with duration info
        logApproval(actuatorID, targetState, maxDurationSeconds);
    } else {
        // Turning OFF - calculate total runtime
        if (state.turnOnTime > 0) {
            uint32_t runtime = (currentTime - state.turnOnTime) / 1000;
            ESP_LOGI(TAG, "APPROVED: %s OFF (ran for %u seconds)",
                     actuatorID, runtime);
        } else {
            ESP_LOGI(TAG, "APPROVED: %s OFF", actuatorID);
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
    // Monitor all actuators for duration violations
    uint32_t currentTime = esphome::millis();

    for (auto& pair : actuators_) {
        const std::string& id = pair.first;
        ActuatorState& state = pair.second;

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

} // namespace actuator_safety_gate
} // namespace esphome
