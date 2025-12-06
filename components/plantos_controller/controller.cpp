#include "controller.h"
#include "esphome/components/plantos_hal/hal.h"
#include "esphome/components/actuator_safety_gate/ActuatorSafetyGate.h"
#include <algorithm> // for std::sort, std::min, std::max

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
    // Yellow pulse - stabilizing pH reading (5 minutes)
    // All pumps OFF for accurate measurement

    uint32_t elapsed = getStateElapsed();

    // On entry: Turn off all pumps for stabilization
    if (elapsed < 100) {
        turnOffAllPumps();
        ESP_LOGI(TAG, "pH measuring: All pumps OFF for 5-minute stabilization");
        ph_readings_.clear(); // Reset readings buffer
        return;
    }

    // Take pH readings every 60 seconds
    uint32_t reading_interval = elapsed / 60000; // Every 60 seconds
    if (reading_interval > state_counter_) {
        state_counter_ = reading_interval;

        if (hasPhValue()) {
            float ph = readPH();
            ph_readings_.push_back(ph);
            ESP_LOGI(TAG, "pH reading #%d: %.2f", ph_readings_.size(), ph);

            // Check for critical pH (out of safe range)
            if (ph < 5.0f || ph > 7.5f) {
                ESP_LOGE(TAG, "CRITICAL pH DETECTED: %.2f (safe range: 5.0-7.5)", ph);
                // Log warning but continue measurement
                // Critical alerts would be handled by StatusLogger (Phase 7)
            }
        } else {
            ESP_LOGW(TAG, "pH sensor has no value - waiting for reading");
        }
    }

    // Wait full 5 minutes before calculating
    if (elapsed < PH_MEASURING_DURATION) {
        return;
    }

    // Measurement period complete - calculate robust average
    if (ph_readings_.empty()) {
        ESP_LOGE(TAG, "No pH readings collected - aborting correction");
        transitionTo(ControllerState::ERROR);
        return;
    }

    ph_current_ = calculateRobustPhAverage();
    ESP_LOGI(TAG, "pH stabilization complete. Robust average: %.2f (from %d readings)",
             ph_current_, ph_readings_.size());

    transitionTo(ControllerState::PH_CALCULATING);
}

void PlantOSController::handlePhCalculating() {
    // Yellow fast blink - determining pH adjustment
    // Decision point: Does pH need correction?

    // Use hardcoded target range for Phase 4
    // Phase 7 will integrate CalendarManager for dynamic targets
    float target_min = PH_TARGET_MIN; // 5.5
    float target_max = PH_TARGET_MAX; // 6.5

    ESP_LOGI(TAG, "pH: %.2f, Target: %.2f-%.2f", ph_current_, target_min, target_max);

    // Check if pH is within target range
    if (ph_current_ >= target_min && ph_current_ <= target_max) {
        ESP_LOGI(TAG, "pH within target range - no correction needed");
        // Phase 7: Clear PSM event here (psm_->clearEvent())
        transitionTo(ControllerState::IDLE);
        return;
    }

    // Check if pH is too low (cannot correct - no base pump)
    if (ph_current_ < target_min) {
        ESP_LOGW(TAG, "pH too low (%.2f) - cannot correct without base pump", ph_current_);
        transitionTo(ControllerState::IDLE);
        return;
    }

    // pH is too high - calculate required acid dose
    uint32_t dose_ms = calculateAcidDuration(ph_current_, target_max);

    // Check minimum dose threshold (avoid tiny corrections)
    if (dose_ms < 1000) {
        ESP_LOGI(TAG, "Calculated dose too small (%u ms < 1000 ms) - skipping correction", dose_ms);
        transitionTo(ControllerState::IDLE);
        return;
    }

    // Check max attempts to prevent infinite loops
    if (ph_attempt_count_ >= MAX_PH_ATTEMPTS) {
        ESP_LOGE(TAG, "Max pH correction attempts reached (%d) - aborting", MAX_PH_ATTEMPTS);
        // Phase 7: Update alert status here
        transitionTo(ControllerState::IDLE);
        return;
    }

    // Proceed with injection
    ph_dose_duration_ms_ = dose_ms;
    ph_attempt_count_++;

    ESP_LOGI(TAG, "pH correction needed: %.2f → %.2f (dose: %u ms, attempt %d/%d)",
             ph_current_, target_max, dose_ms, ph_attempt_count_, MAX_PH_ATTEMPTS);

    transitionTo(ControllerState::PH_INJECTING);
}

void PlantOSController::handlePhInjecting() {
    // Cyan pulse - acid dosing in progress
    // Acid pump + Air pump active

    uint32_t elapsed = getStateElapsed();

    // On entry: Activate pumps
    if (elapsed < 100) {
        // Calculate duration in seconds (round up)
        uint32_t duration_sec = (ph_dose_duration_ms_ + 999) / 1000;

        ESP_LOGI(TAG, "Starting acid injection: %u ms (%u sec)", ph_dose_duration_ms_, duration_sec);

        // Start air pump first for immediate mixing
        requestPump(AIR_PUMP, true, 0); // Continuous during injection

        // Request acid pump via SafetyGate
        if (!requestPump(ACID_PUMP, true, duration_sec)) {
            ESP_LOGE(TAG, "Acid pump rejected by SafetyGate - aborting");
            requestPump(AIR_PUMP, false); // Stop air pump too
            transitionTo(ControllerState::ERROR);
            return;
        }

        ESP_LOGI(TAG, "Acid pump active (Air pump mixing)");
        return;
    }

    // Wait for injection duration + 200ms safety margin
    uint32_t total_duration = ph_dose_duration_ms_ + 200;

    if (elapsed >= total_duration) {
        // Injection complete - explicitly stop acid pump
        requestPump(ACID_PUMP, false);

        ESP_LOGI(TAG, "Acid dosing complete (%u ms) - starting 2-minute mixing phase", ph_dose_duration_ms_);
        transitionTo(ControllerState::PH_MIXING);
    }
}

void PlantOSController::handlePhMixing() {
    // Blue pulse - mixing after acid injection
    // Air pump runs for 2 minutes to distribute acid throughout tank

    if (getStateElapsed() >= PH_MIXING_DURATION) {
        // Mixing complete - stop air pump
        requestPump(AIR_PUMP, false);
        ESP_LOGI(TAG, "pH mixing complete (2 minutes)");

        // Loop back to PH_MEASURING to verify correction
        // Will continue until pH is in range OR max attempts reached
        ESP_LOGI(TAG, "Re-measuring pH to verify correction (attempt %d/%d)",
                 ph_attempt_count_, MAX_PH_ATTEMPTS);

        transitionTo(ControllerState::PH_MEASURING);
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
    if (current_state_ != ControllerState::IDLE && current_state_ != ControllerState::MAINTENANCE) {
        ESP_LOGW(TAG, "Cannot start pH correction - system busy (state: %d)", static_cast<int>(current_state_));
        return;
    }

    ESP_LOGI(TAG, "Starting pH correction sequence");

    // Reset pH correction state
    ph_attempt_count_ = 0;
    ph_readings_.clear();
    ph_current_ = 0.0f;
    ph_dose_duration_ms_ = 0;

    // Phase 7: Log event for persistence (PSM)
    // if (psm_) {
    //     psm_->logEvent("PH_CORRECTION", 0); // 0 = STARTED
    // }

    transitionTo(ControllerState::PH_MEASURING);
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

// ============================================================================
// pH Correction Helpers
// ============================================================================

uint32_t PlantOSController::calculateAcidDuration(float current_ph, float target_ph_max) {
    // Calculate acid dose based on pH offset
    // Formula: dose_seconds = pH_offset * calibration_factor
    // This is a simplified calculation - production systems would use
    // tank volume, acid concentration, and empirical calibration data

    if (current_ph <= target_ph_max) {
        return 0; // No correction needed
    }

    float ph_offset = current_ph - target_ph_max;

    // Calibration factor: 2 seconds per 0.1 pH unit
    // Example: pH 7.0 → 6.5 = 0.5 offset = 10 seconds
    float dose_seconds = ph_offset * 20.0f; // 20 seconds per 1.0 pH

    // Convert to milliseconds
    uint32_t dose_ms = static_cast<uint32_t>(dose_seconds * 1000.0f);

    // Apply safety limits
    if (dose_ms > 30000) dose_ms = 30000; // Max 30 seconds
    if (dose_ms < 1000) dose_ms = 1000;   // Min 1 second

    return dose_ms;
}

float PlantOSController::calculateRobustPhAverage() {
    if (ph_readings_.empty()) {
        return 0.0f;
    }

    // Single reading - return as-is
    if (ph_readings_.size() == 1) {
        return ph_readings_[0];
    }

    // Sort readings for robust averaging
    std::vector<float> sorted = ph_readings_;
    std::sort(sorted.begin(), sorted.end());

    // Reject 10% from each end (outliers)
    size_t reject_count = sorted.size() / 10;
    if (reject_count == 0 && sorted.size() > 2) {
        reject_count = 1; // Reject at least 1 from each end
    }

    // Calculate average of middle values
    float sum = 0.0f;
    size_t count = 0;

    for (size_t i = reject_count; i < sorted.size() - reject_count; i++) {
        sum += sorted[i];
        count++;
    }

    if (count == 0) {
        // Fallback: use all readings
        for (float reading : sorted) {
            sum += reading;
        }
        count = sorted.size();
    }

    return sum / count;
}

bool PlantOSController::isPhStable() {
    // Need at least 3 readings to determine stability
    if (ph_readings_.size() < 3) {
        return false;
    }

    // Check if last 3 readings are within 0.1 pH
    constexpr float STABILITY_THRESHOLD = 0.1f;

    size_t last_idx = ph_readings_.size() - 1;
    float reading_1 = ph_readings_[last_idx];
    float reading_2 = ph_readings_[last_idx - 1];
    float reading_3 = ph_readings_[last_idx - 2];

    float max_val = std::max({reading_1, reading_2, reading_3});
    float min_val = std::min({reading_1, reading_2, reading_3});

    return (max_val - min_val) <= STABILITY_THRESHOLD;
}

} // namespace plantos_controller
