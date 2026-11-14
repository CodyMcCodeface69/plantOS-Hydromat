// Water operations: tank fill, tank empty and the fill step of the feed / reservoir change sequence.
// Part of PlantOSController (declared in controller.h). FSMINFO.md is authoritative for FSM behavior.
#include "controller.h"
#include "esphome/components/plantos_hal/hal.h"
#include "esphome/components/actuator_safety_gate/ActuatorSafetyGate.h"
#include "esphome/components/persistent_state_manager/persistent_state_manager.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace plantos_controller {

// ============================================================================
// Shared helpers
// ============================================================================

void PlantOSController::stopWaterActuator(const char *id) {
    if (safety_gate_) {
        safety_gate_->executeCommand(id, false, 0);
    }
}

void PlantOSController::clearPsmEvent() {
    if (psm_) {
        psm_->clearEvent();
    }
}

bool PlantOSController::clearPsmEventIf(const char *event_id) {
    if (psm_ && psm_->hasEvent() && strcmp(psm_->getLastEvent().eventID, event_id) == 0) {
        psm_->clearEvent();
        return true;
    }
    return false;
}

void PlantOSController::alertWaterSensorsMissing(const char *operation) {
    status_logger_.updateAlertWithContext(
        "HARDWARE_WATER_SENSORS_MISSING",
        "Water level sensors unavailable - fill aborted",
        "hasWaterLevelSensors() returned false at fill start",
        "Check the level sensors (HIGH=GPIO17, LOW=GPIO23, EMPTY=GPIO22) and the HAL config",
        operation,
        "Fill refused to prevent overflow.",
        0
    );
}

void PlantOSController::queuePostFillChecks() {
    // Post-fill sequence: IDLE dispatches EC check -> pH correction
    auto_ec_check_pending_ = true;
    auto_ph_correction_pending_ = true;
    ph_post_fill_stabilize_ = true;  // Extended 10-min pH stabilization after fill
    ec_cycle_water_filled_ = false;  // Reset EC cycle guard
    ph_cycle_water_filled_ = false;  // Reset pH cycle guard
    ec_attempt_count_ = 0;           // Reset EC cycle state for clean measurement
    ec_total_ml_per_L_ = 0.0f;
}

void PlantOSController::finishDrain() {
    // auto_ph_correction_pending_ marks a reservoir change: continue with the fill phase
    transitionTo(auto_ph_correction_pending_ ? ControllerState::FEED_FILLING : ControllerState::IDLE);
}

// ============================================================================
// WATER_FILLING
// ============================================================================

void PlantOSController::handleWaterFilling() {
    // Blue solid LED handled by LedBehaviorSystem
    uint32_t elapsed = getStateElapsed();

    // FILL TIMEOUT: 10 minutes - safety backup if the HIGH sensor never triggers
    // (CONFIGURE ACCORDING TO: tank volume and Mag Valve flowspeed)
    static constexpr uint32_t FILL_DURATION_MS = 600000;

    // ENTRY: open the valve once per entry (state_entry_executed_ is reset by transitionTo)
    if (!state_entry_executed_) {
        state_entry_executed_ = true;

        // SAFETY: Abort if water level sensors are unavailable - filling blind
        // on a timeout with no level feedback risks tank overflow
        if (!hal_ || !hal_->hasWaterLevelSensors()) {
            ESP_LOGE(TAG, "[WATER_FILLING] No water level sensors - refusing blind fill");
            alertWaterSensorsMissing("Water filling refused");
            auto_ph_correction_pending_ = false;
            clearPsmEvent();
            transitionTo(ControllerState::IDLE);
            return;
        }

        // Request water valve open; ASG validates against its 600s max duration
        // and force-closes the valve as a hardware-level backstop
        if (safety_gate_ && !safety_gate_->executeCommand(WATER_VALVE, true, FILL_DURATION_MS / 1000)) {
            ESP_LOGE(TAG, "[WATER_FILLING] SafetyGate rejected WaterValve (%u s)", (unsigned)(FILL_DURATION_MS / 1000));
            status_logger_.updateAlertWithContext(
                "PUMP_REJECTION_WATER_VALVE",
                "Water valve rejected by SafetyGate",
                "SafetyGate rejected water valve command",
                "Check the valve and the SafetyGate max duration",
                "Water filling aborted",
                "Manual intervention required.",
                0
            );
            clearPsmEvent();
            transitionTo(ControllerState::IDLE);
            return;
        }

        ESP_LOGI(TAG, "[WATER_FILLING] Valve open (max %u s)", (unsigned)(FILL_DURATION_MS / 1000));
        return;
    }

    if (!hal_->hasWaterLevelSensors()) {
        // SAFETY: Sensors disappeared mid-fill - close valve immediately and abort
        ESP_LOGE(TAG, "[WATER_FILLING] Water level sensors lost - valve closed, fill aborted");
        stopWaterActuator(WATER_VALVE);
        clearPsmEvent();
        auto_ph_correction_pending_ = false;
        transitionTo(ControllerState::IDLE);
        return;
    }

    // SUCCESS: High level reached
    if (hal_->readWaterLevelHigh()) {
        ESP_LOGI(TAG, "[WATER_FILLING] HIGH reached after %u s - valve closed, EC check and pH correction queued",
                 (unsigned)(elapsed / 1000));
        stopWaterActuator(WATER_VALVE);
        status_logger_.resolveAlert("PUMP_REJECTION_WATER_VALVE");
        status_logger_.resolveAlert("HARDWARE_WATER_SENSOR_HIGH");
        clearPsmEvent();
        queuePostFillChecks();
        transitionTo(ControllerState::IDLE);
        return;
    }

    // TIMEOUT: Safety backup - 10 minute max fill time
    if (elapsed >= FILL_DURATION_MS) {
        ESP_LOGE(TAG, "[WATER_FILLING] HIGH not reached in %u s - valve closed (sensor fault or low pressure?), "
                 "EC check and pH correction queued", (unsigned)(FILL_DURATION_MS / 1000));
        stopWaterActuator(WATER_VALVE);
        clearPsmEvent();
        status_logger_.updateAlertWithContext(
            "HARDWARE_WATER_SENSOR_HIGH",
            "HIGH water level sensor never triggered during fill",
            "No HIGH signal within the 10-minute fill timeout",
            "Check the HIGH sensor (GPIO17) and the water pressure",
            "Water filling ended by timeout",
            "Tank may be under-filled or the sensor faulty. Check by hand.",
            0
        );
        queuePostFillChecks();
        transitionTo(ControllerState::IDLE);
        return;
    }
}

// ============================================================================
// WATER_EMPTYING
// ============================================================================

void PlantOSController::handleWaterEmptying() {
    // Purple pulse LED handled by LedBehaviorSystem
    uint32_t elapsed = getStateElapsed();

    // Empty duration: 300 seconds (fallback if sensors unavailable)
    static constexpr uint32_t EMPTY_DURATION_MS = 300000;

    // Shelly integration switched off mid-drain: the HAL already queued WastewaterPump OFF.
    // Abort the drain (and a running reservoir change) to IDLE.
    if (!hal_->isShellyEnabled()) {
        ESP_LOGE(TAG, "[WATER_EMPTYING] Shelly integration disabled - drain aborted");
        stopWaterActuator(WASTEWATER_PUMP);
        clearPsmEvent();
        auto_ph_correction_pending_ = false;
        transitionTo(ControllerState::IDLE);
        return;
    }

    // ENTRY: start the pump once per entry (state_entry_executed_ is reset by transitionTo)
    if (!state_entry_executed_) {
        state_entry_executed_ = true;

        // Check EMPTY sensor before starting (prevent dry-pump immediately)
        if (hal_->hasWaterLevelSensors() && !hal_->readWaterLevelEmpty()) {
            ESP_LOGW(TAG, "[WATER_EMPTYING] EMPTY already off - nothing to drain");
            clearPsmEvent();
            finishDrain();
            return;
        }

        // Request wastewater pump on (300s max duration as safety backup)
        if (safety_gate_ && !safety_gate_->executeCommand(WASTEWATER_PUMP, true, EMPTY_DURATION_MS / 1000)) {
            ESP_LOGE(TAG, "[WATER_EMPTYING] SafetyGate rejected WastewaterPump (%u s)%s",
                     (unsigned)(EMPTY_DURATION_MS / 1000), auto_ph_correction_pending_ ? " - reservoir change aborted" : "");
            status_logger_.updateAlertWithContext(
                "PUMP_REJECTION_WASTEWATER",
                "Wastewater pump rejected by SafetyGate",
                "SafetyGate rejected wastewater pump command",
                "Check the pump and the SafetyGate max duration",
                "Water emptying aborted",
                "Manual intervention required.",
                0
            );
            clearPsmEvent();
            auto_ph_correction_pending_ = false;  // reservoir change sequence aborted
            transitionTo(ControllerState::IDLE);
            return;
        }

        ESP_LOGI(TAG, "[WATER_EMPTYING] Wastewater pump on (max %u s)", (unsigned)(EMPTY_DURATION_MS / 1000));
        return;
    }

    bool sensors = hal_->hasWaterLevelSensors();

    // SUCCESS: EMPTY sensor OFF (tank below minimum safe level)
    if (sensors && !hal_->readWaterLevelEmpty()) {
        ESP_LOGI(TAG, "[WATER_EMPTYING] EMPTY cleared after %u s - pump off", (unsigned)(elapsed / 1000));
        stopWaterActuator(WASTEWATER_PUMP);
        status_logger_.resolveAlert("PUMP_REJECTION_WASTEWATER");
        status_logger_.resolveAlert("HARDWARE_WATER_SENSOR_EMPTY");
        clearPsmEvent();
        finishDrain();
        return;
    }

    // TIMEOUT: Safety backup - 300s max drain time (the only end without level sensors)
    if (elapsed >= EMPTY_DURATION_MS) {
        stopWaterActuator(WASTEWATER_PUMP);
        clearPsmEvent();
        if (sensors) {
            ESP_LOGE(TAG, "[WATER_EMPTYING] EMPTY not cleared in %u s - pump off (sensor fault or clog?)",
                     (unsigned)(EMPTY_DURATION_MS / 1000));
            status_logger_.updateAlertWithContext(
                "HARDWARE_WATER_SENSOR_EMPTY",
                "EMPTY water level sensor never cleared during drain",
                "EMPTY still on after the 300 s drain timeout",
                "Check the EMPTY sensor (GPIO22) and the drain for clogs",
                "Water emptying ended by timeout",
                "Tank may not be fully drained or the sensor faulty. Check by hand.",
                0
            );
        } else {
            ESP_LOGW(TAG, "[WATER_EMPTYING] No level sensors - pump off after %u s", (unsigned)(EMPTY_DURATION_MS / 1000));
        }
        finishDrain();
        return;
    }
}

// ============================================================================
// FEED_FILLING
// ============================================================================

void PlantOSController::handleFeedFilling() {
    // Blue solid LED handled by LedBehaviorSystem (same as WATER_FILLING)
    // This is the first phase of the Feed operation
    uint32_t elapsed = getStateElapsed();

    // Max fill duration = (volume_mL / valve flow mL/s) x 1.2, recomputed every call as before.
    // Normal feed fills LOW->HIGH (delta volume); reservoir change fills EMPTY->HIGH (total volume).
    uint32_t max_fill_duration_ms = 600000;  // Fallback without HAL: 10 minutes
    uint32_t max_fill_duration_sec = 600;    // For SafetyGate command
    float tank_volume_L = 0.0f;
    if (hal_) {
        tank_volume_L = is_reservoir_change_ ? hal_->getTotalTankVolume() : hal_->getTankVolumeDelta();
        float fill_duration_sec = hal_->valveflow(tank_volume_L * 1000.0f) * 1.2f;
        max_fill_duration_ms = static_cast<uint32_t>(fill_duration_sec * 1000.0f);
        max_fill_duration_sec = static_cast<uint32_t>(fill_duration_sec);
    }

    // ENTRY: safety checks and open the valve (only once)
    if (!state_entry_executed_) {
        state_entry_executed_ = true;

        // SAFETY: Abort if water level sensors are unavailable - filling blind
        // on a timeout with no level feedback risks tank overflow
        if (!hal_ || !hal_->hasWaterLevelSensors()) {
            ESP_LOGE(TAG, "[FEED_FILLING] No water level sensors - refusing blind fill");
            alertWaterSensorsMissing("Feed sequence aborted before filling");
            auto_ph_correction_pending_ = false;
            clearPsmEvent();
            transitionTo(ControllerState::IDLE);
            return;
        }

        // Safety check - tank must be EMPTY before filling (all 3 sensors OFF)
        bool high_sensor = hal_->readWaterLevelHigh();
        bool low_sensor = hal_->readWaterLevelLow();
        bool empty_sensor = hal_->readWaterLevelEmpty();
        if (high_sensor || low_sensor || empty_sensor) {
            ESP_LOGE(TAG, "[FEED_FILLING] Tank not empty (H/L/E=%d%d%d) - feed aborted, drain it first",
                     high_sensor, low_sensor, empty_sensor);
            auto_ph_correction_pending_ = false;
            clearPsmEvent();
            transitionTo(ControllerState::IDLE);
            return;
        }

        // Request water valve open with calculated max duration
        if (safety_gate_ && !safety_gate_->executeCommand(WATER_VALVE, true, max_fill_duration_sec)) {
            ESP_LOGE(TAG, "[FEED_FILLING] SafetyGate rejected WaterValve (%u s) - feed aborted",
                     (unsigned)max_fill_duration_sec);
            auto_ph_correction_pending_ = false;
            clearPsmEvent();
            transitionTo(ControllerState::IDLE);
            return;
        }

        ESP_LOGI(TAG, "[FEED_FILLING] Valve open, max %u s (%.1f L %s volume + 20%%)", (unsigned)max_fill_duration_sec,
                 tank_volume_L, is_reservoir_change_ ? "total" : "delta");
        return;
    }

    // SUCCESS: Tank full when BOTH HIGH and LOW are ON
    if (hal_->hasWaterLevelSensors() && hal_->readWaterLevelHigh() && hal_->readWaterLevelLow()) {
        ESP_LOGI(TAG, "[FEED_FILLING] Full after %u s - valve closed", (unsigned)(elapsed / 1000));
        stopWaterActuator(WATER_VALVE);
        // auto_ph_correction_pending_ stays set for the pH check after feeding
        transitionTo(ControllerState::FEEDING);
        return;
    }

    // TIMEOUT: Safety backup - calculated max fill time
    if (elapsed >= max_fill_duration_ms) {
        stopWaterActuator(WATER_VALVE);
        if (hal_->hasWaterLevelSensors()) {
            ESP_LOGE(TAG, "[FEED_FILLING] HIGH+LOW not reached in %u s - valve closed, dosing anyway "
                     "(sensor fault or low pressure?)", (unsigned)max_fill_duration_sec);
        } else {
            ESP_LOGW(TAG, "[FEED_FILLING] Timeout %u s - valve closed, dosing", (unsigned)max_fill_duration_sec);
        }
        transitionTo(ControllerState::FEEDING);
        return;
    }
}

} // namespace plantos_controller
