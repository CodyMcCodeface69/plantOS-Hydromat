#include "controller.h"
#include "esphome/components/plantos_hal/hal.h"
#include "esphome/components/actuator_safety_gate/ActuatorSafetyGate.h"

namespace plantos_controller {

PlantOSController::PlantOSController() {
    // Initialize LED behavior system
    led_behaviors_ = std::make_unique<LedBehaviorSystem>();
}

// ============================================================================
// ESPHome Component Lifecycle
// ============================================================================

void PlantOSController::setup() {
    ESP_LOGI(TAG, "PlantOS Unified Controller initializing...");

    // Verify critical dependencies
    if (!hal_) {
        ESP_LOGE(TAG, "HAL not configured - Controller cannot function!");
        return;
    }

    if (!safety_gate_) {
        ESP_LOGW(TAG, "SafetyGate not configured - Actuator control disabled");
    }

    ESP_LOGI(TAG, "Controller initialized - Starting in INIT state");
    transitionTo(ControllerState::INIT);
}

void PlantOSController::loop() {
    if (!hal_) {
        return; // Cannot operate without HAL
    }

    // Update LED behavior for current state
    uint32_t elapsed = getStateElapsed();
    led_behaviors_->update(current_state_, elapsed, hal_);

    // Call state-specific handler
    switch (current_state_) {
        case ControllerState::INIT:
            handleInit();
            break;

        case ControllerState::IDLE:
            handleIdle();
            break;

        case ControllerState::MAINTENANCE:
            handleMaintenance();
            break;

        case ControllerState::ERROR:
            handleError();
            break;

        case ControllerState::PH_MEASURING:
            handlePhMeasuring();
            break;

        case ControllerState::PH_CALCULATING:
            handlePhCalculating();
            break;

        case ControllerState::PH_INJECTING:
            handlePhInjecting();
            break;

        case ControllerState::PH_MIXING:
            handlePhMixing();
            break;

        case ControllerState::PH_CALIBRATING:
            handlePhCalibrating();
            break;

        case ControllerState::FEEDING:
            handleFeeding();
            break;

        case ControllerState::WATER_FILLING:
            handleWaterFilling();
            break;

        case ControllerState::WATER_EMPTYING:
            handleWaterEmptying();
            break;
    }
}

// ============================================================================
// State Handlers
// ============================================================================

void PlantOSController::handleInit() {
    // Boot sequence: Red → Yellow → Green (3 seconds)
    // LED behavior handles visual feedback

    if (getStateElapsed() >= INIT_DURATION) {
        ESP_LOGI(TAG, "Boot sequence complete");
        transitionTo(ControllerState::IDLE);
    }
}

void PlantOSController::handleIdle() {
    // Breathing green - waiting for commands
    // This is the default ready state

    // Future: Check for scheduled tasks, sensor thresholds, etc.
}

void PlantOSController::handleMaintenance() {
    // Solid yellow - all automation disabled
    // User must manually exit this mode
}

void PlantOSController::handleError() {
    // Fast red flash - error condition
    // Automatically return to INIT after timeout

    if (getStateElapsed() >= ERROR_DURATION) {
        ESP_LOGI(TAG, "Error timeout - returning to INIT");
        transitionTo(ControllerState::INIT);
    }
}

void PlantOSController::handlePhMeasuring() {
    // Yellow pulse - stabilizing pH reading
    // Wait for sensor to stabilize before measuring

    if (getStateElapsed() >= PH_MEASURING_DURATION) {
        ESP_LOGI(TAG, "pH measurement period complete");
        transitionTo(ControllerState::PH_CALCULATING);
    }
}

void PlantOSController::handlePhCalculating() {
    // Yellow fast blink - determining pH adjustment
    // Read pH and decide on correction

    float ph = readPH();

    if (!hasPhValue()) {
        ESP_LOGW(TAG, "No pH value available - aborting pH correction");
        transitionTo(ControllerState::ERROR);
        return;
    }

    ESP_LOGI(TAG, "Current pH: %.2f (target: %.2f - %.2f)", ph, PH_TARGET_MIN, PH_TARGET_MAX);

    if (ph >= PH_TARGET_MIN && ph <= PH_TARGET_MAX) {
        ESP_LOGI(TAG, "pH in target range - no correction needed");
        transitionTo(ControllerState::IDLE);
    } else if (ph > PH_TARGET_MAX) {
        // pH too high - need acid injection
        ESP_LOGI(TAG, "pH too high (%.2f) - starting acid injection", ph);

        // Calculate injection duration (simplified - production would use calibration)
        float ph_offset = ph - PH_TARGET_MAX;
        uint32_t injection_seconds = static_cast<uint32_t>(ph_offset * 2.0f); // 2 seconds per 0.1 pH
        injection_seconds = std::min(injection_seconds, 10u); // Max 10 seconds

        state_counter_ = injection_seconds; // Store for use in PH_INJECTING
        transitionTo(ControllerState::PH_INJECTING);
    } else {
        // pH too low - cannot correct (no base pump)
        ESP_LOGW(TAG, "pH too low (%.2f) - cannot correct without base pump", ph);
        transitionTo(ControllerState::IDLE);
    }
}

void PlantOSController::handlePhInjecting() {
    // Cyan pulse - acid dosing in progress
    // Turn on acid pump and air pump

    uint32_t elapsed = getStateElapsed();

    if (elapsed == 0) {
        // Just entered state - start pumps
        uint32_t duration = state_counter_; // Seconds calculated in PH_CALCULATING

        ESP_LOGI(TAG, "Starting acid injection for %u seconds", duration);

        // Request acid pump via SafetyGate
        if (!requestPump(ACID_PUMP, true, duration)) {
            ESP_LOGE(TAG, "Failed to start acid pump - aborting");
            transitionTo(ControllerState::ERROR);
            return;
        }

        // Start air pump for mixing (no duration limit during injection)
        requestPump(AIR_PUMP, true, 0);
    }

    // Wait for injection duration + margin
    uint32_t injection_duration_ms = (state_counter_ * 1000) + 200;

    if (elapsed >= injection_duration_ms) {
        // Injection complete - stop acid pump, continue air pump for mixing
        requestPump(ACID_PUMP, false);
        ESP_LOGI(TAG, "Acid injection complete - starting mixing phase");
        transitionTo(ControllerState::PH_MIXING);
    }
}

void PlantOSController::handlePhMixing() {
    // Blue pulse - mixing after acid injection
    // Air pump runs to mix acid throughout tank

    if (getStateElapsed() >= PH_MIXING_DURATION) {
        // Mixing complete - stop air pump
        requestPump(AIR_PUMP, false);
        ESP_LOGI(TAG, "pH mixing complete");

        // Check if we should measure again or return to idle
        // For now, return to idle (production would loop back to measuring)
        transitionTo(ControllerState::IDLE);
    }
}

void PlantOSController::handlePhCalibrating() {
    // Yellow fast blink - pH sensor calibration
    // This state is entered manually via service call
    // For now, just a placeholder - actual calibration would use HAL

    ESP_LOGD(TAG, "pH calibration mode - waiting for manual completion");
    // User must manually exit this state
}

void PlantOSController::handleFeeding() {
    // Orange pulse - nutrient dosing
    // Placeholder for nutrient dosing logic

    uint32_t elapsed = getStateElapsed();

    if (elapsed == 0) {
        ESP_LOGI(TAG, "Starting feeding sequence");
        // Future: Dose nutrients A, B, C based on calendar schedule
        // For now, just a 5-second placeholder
    }

    if (elapsed >= 5000) {
        ESP_LOGI(TAG, "Feeding complete");
        transitionTo(ControllerState::IDLE);
    }
}

void PlantOSController::handleWaterFilling() {
    // Blue solid - fresh water addition
    // Placeholder for water fill logic

    uint32_t elapsed = getStateElapsed();

    if (elapsed == 0) {
        ESP_LOGI(TAG, "Starting water fill");
        // Open water valve
        requestValve(WATER_VALVE, true, 60); // Max 60 seconds
    }

    // For now, auto-complete after 10 seconds
    // Production would use water level sensor
    if (elapsed >= 10000) {
        requestValve(WATER_VALVE, false);
        ESP_LOGI(TAG, "Water fill complete");
        transitionTo(ControllerState::IDLE);
    }
}

void PlantOSController::handleWaterEmptying() {
    // Purple pulse - wastewater removal
    // Placeholder for water empty logic

    uint32_t elapsed = getStateElapsed();

    if (elapsed == 0) {
        ESP_LOGI(TAG, "Starting water drain");
        // Turn on wastewater pump
        requestPump(WASTEWATER_PUMP, true, 180); // Max 3 minutes
    }

    // For now, auto-complete after 15 seconds
    // Production would use water level sensor
    if (elapsed >= 15000) {
        requestPump(WASTEWATER_PUMP, false);
        ESP_LOGI(TAG, "Water drain complete");
        transitionTo(ControllerState::IDLE);
    }
}

// ============================================================================
// Public API
// ============================================================================

void PlantOSController::startPhCorrection() {
    if (current_state_ == ControllerState::IDLE || current_state_ == ControllerState::MAINTENANCE) {
        ESP_LOGI(TAG, "Starting pH correction sequence");
        transitionTo(ControllerState::PH_MEASURING);
    } else {
        ESP_LOGW(TAG, "Cannot start pH correction - system busy");
    }
}

void PlantOSController::startFeeding() {
    if (current_state_ == ControllerState::IDLE || current_state_ == ControllerState::MAINTENANCE) {
        ESP_LOGI(TAG, "Starting feeding sequence");
        transitionTo(ControllerState::FEEDING);
    } else {
        ESP_LOGW(TAG, "Cannot start feeding - system busy");
    }
}

void PlantOSController::startFillTank() {
    if (current_state_ == ControllerState::IDLE || current_state_ == ControllerState::MAINTENANCE) {
        ESP_LOGI(TAG, "Starting tank fill");
        transitionTo(ControllerState::WATER_FILLING);
    } else {
        ESP_LOGW(TAG, "Cannot start fill - system busy");
    }
}

void PlantOSController::startEmptyTank() {
    if (current_state_ == ControllerState::IDLE || current_state_ == ControllerState::MAINTENANCE) {
        ESP_LOGI(TAG, "Starting tank drain");
        transitionTo(ControllerState::WATER_EMPTYING);
    } else {
        ESP_LOGW(TAG, "Cannot start drain - system busy");
    }
}

bool PlantOSController::toggleMaintenanceMode(bool enable) {
    if (enable) {
        if (current_state_ != ControllerState::MAINTENANCE) {
            ESP_LOGI(TAG, "Entering maintenance mode");
            turnOffAllPumps();
            transitionTo(ControllerState::MAINTENANCE);
            return true;
        }
    } else {
        if (current_state_ == ControllerState::MAINTENANCE) {
            ESP_LOGI(TAG, "Exiting maintenance mode");
            transitionTo(ControllerState::IDLE);
            return true;
        }
    }
    return false;
}

void PlantOSController::resetToInit() {
    ESP_LOGI(TAG, "Manual reset requested");
    turnOffAllPumps();
    transitionTo(ControllerState::INIT);
}

// ============================================================================
// State Transition
// ============================================================================

void PlantOSController::transitionTo(ControllerState newState) {
    if (newState == current_state_) {
        return; // Already in this state
    }

    ESP_LOGI(TAG, "State transition: %d → %d", static_cast<int>(current_state_), static_cast<int>(newState));

    current_state_ = newState;
    state_start_time_ = esphome::millis();
    state_counter_ = 0;

    // LED behavior transition is handled automatically in loop()
    // via led_behaviors_->update()
}

uint32_t PlantOSController::getStateElapsed() const {
    return esphome::millis() - state_start_time_;
}

// ============================================================================
// Actuator Control Helpers
// ============================================================================

bool PlantOSController::requestPump(const std::string& pumpId, bool state, uint32_t durationSec) {
    if (!safety_gate_) {
        ESP_LOGW(TAG, "SafetyGate not available - cannot control %s", pumpId.c_str());
        return false;
    }

    return safety_gate_->executeCommand(pumpId.c_str(), state, durationSec);
}

bool PlantOSController::requestValve(const std::string& valveId, bool state, uint32_t durationSec) {
    if (!safety_gate_) {
        ESP_LOGW(TAG, "SafetyGate not available - cannot control %s", valveId.c_str());
        return false;
    }

    return safety_gate_->executeCommand(valveId.c_str(), state, durationSec);
}

void PlantOSController::turnOffAllPumps() {
    ESP_LOGI(TAG, "Emergency stop - turning off all pumps");

    if (!safety_gate_) {
        return;
    }

    // Turn off all pumps
    requestPump(ACID_PUMP, false);
    requestPump(NUTRIENT_PUMP_A, false);
    requestPump(NUTRIENT_PUMP_B, false);
    requestPump(NUTRIENT_PUMP_C, false);
    requestPump(WASTEWATER_PUMP, false);
    requestPump(AIR_PUMP, false);

    // Close all valves
    requestValve(WATER_VALVE, false);
}

// ============================================================================
// Sensor Helpers
// ============================================================================

float PlantOSController::readPH() {
    if (!hal_) {
        return 0.0f;
    }
    return hal_->readPH();
}

bool PlantOSController::hasPhValue() {
    if (!hal_) {
        return false;
    }
    return hal_->hasPhValue();
}

} // namespace plantos_controller
