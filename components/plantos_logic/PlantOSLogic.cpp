#include "PlantOSLogic.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cmath>

namespace esphome {
namespace plantos_logic {

static const char* TAG = "plantos_logic";

// pH configuration constants
static constexpr float PH_CRITICAL_MIN = 5.0f;
static constexpr float PH_CRITICAL_MAX = 7.5f;
static constexpr uint32_t PH_MIN_DOSE_MS = 1000;  // Minimum dosing duration
static constexpr uint32_t PH_STABILIZATION_TIME_MS = 300000;  // 5 minutes
static constexpr uint32_t PH_MIXING_TIME_MS = 120000;  // 2 minutes
static constexpr uint32_t PH_READING_INTERVAL_MS = 60000;  // 1 minute between readings
static constexpr uint8_t PH_READINGS_REQUIRED = 5;  // Number of readings for robust average
static constexpr float PH_STABILITY_THRESHOLD = 0.1f;  // Max difference for stability

PlantOSLogic::PlantOSLogic() {}

void PlantOSLogic::setup() {
    ESP_LOGI(TAG, "Initializing PlantOSLogic...");

    // Verify dependencies
    if (!this->safety_gate_) {
        ESP_LOGE(TAG, "ActuatorSafetyGate not configured!");
    }
    if (!this->psm_) {
        ESP_LOGE(TAG, "PersistentStateManager not configured!");
    }
    if (!this->status_logger_) {
        ESP_LOGE(TAG, "CentralStatusLogger not configured!");
    }
    if (!this->calendar_) {
        ESP_LOGE(TAG, "CalendarManager not configured!");
    }
    if (!this->ph_sensor_) {
        ESP_LOGW(TAG, "pH sensor not configured - pH routines will not work");
    }

    // Initialize FSM to IDLE
    this->current_status_ = LogicStatus::IDLE;
    this->state_start_time_ = millis();

    // Publish initial state
    this->publish_state();

    ESP_LOGI(TAG, "PlantOSLogic initialized - State: IDLE");
}

void PlantOSLogic::loop() {
    // Execute current state handler
    switch (this->current_status_) {
        case LogicStatus::IDLE:
            this->handle_idle();
            break;
        case LogicStatus::PH_CORRECTION_DUE:
            this->handle_ph_correction_due();
            break;
        case LogicStatus::PH_MEASURING:
            this->handle_ph_measuring();
            break;
        case LogicStatus::PH_CALCULATING:
            this->handle_ph_calculating();
            break;
        case LogicStatus::PH_INJECTING:
            this->handle_ph_injecting();
            break;
        case LogicStatus::PH_MIXING:
            this->handle_ph_mixing();
            break;
        case LogicStatus::PH_CALIBRATING:
            this->handle_ph_calibrating();
            break;
        case LogicStatus::FEEDING_DUE:
            this->handle_feeding_due();
            break;
        case LogicStatus::FEEDING_INJECTING:
            this->handle_feeding_injecting();
            break;
        case LogicStatus::WATER_MANAGEMENT_DUE:
            this->handle_water_management_due();
            break;
        case LogicStatus::WATER_FILLING:
            this->handle_water_filling();
            break;
        case LogicStatus::WATER_EMPTYING:
            this->handle_water_emptying();
            break;
    }
}

// ========== Public API Methods ==========

void PlantOSLogic::start_ph_correction() {
    ESP_LOGI(TAG, "Starting pH correction sequence");

    if (this->current_status_ != LogicStatus::IDLE) {
        ESP_LOGW(TAG, "Cannot start pH correction - system not IDLE (current: %s)",
                 this->get_status_string());
        return;
    }

    // Log critical event to PSM
    if (this->psm_) {
        this->psm_->logEvent("PH_CORRECTION", 0);  // 0 = STARTED
    }

    // Reset pH correction state
    this->ph_attempt_count_ = 0;
    this->ph_readings_.clear();

    // Transition to pH correction
    this->transition_to(LogicStatus::PH_CORRECTION_DUE);
}

void PlantOSLogic::start_ph_measurement_only() {
    ESP_LOGI(TAG, "Starting pH measurement only");

    if (this->current_status_ != LogicStatus::IDLE) {
        ESP_LOGW(TAG, "Cannot start pH measurement - system not IDLE (current: %s)",
                 this->get_status_string());
        return;
    }

    // Reset pH state and transition directly to measuring
    this->ph_readings_.clear();
    this->transition_to(LogicStatus::PH_MEASURING);
}

void PlantOSLogic::start_feeding() {
    ESP_LOGI(TAG, "Starting feeding sequence");

    if (this->current_status_ != LogicStatus::IDLE) {
        ESP_LOGW(TAG, "Cannot start feeding - system not IDLE (current: %s)",
                 this->get_status_string());
        return;
    }

    // Log critical event to PSM
    if (this->psm_) {
        this->psm_->logEvent("FEEDING", 0);  // 0 = STARTED
    }

    this->transition_to(LogicStatus::FEEDING_DUE);
}

void PlantOSLogic::start_fill_tank() {
    ESP_LOGI(TAG, "Starting tank fill sequence");

    if (this->current_status_ != LogicStatus::IDLE) {
        ESP_LOGW(TAG, "Cannot start fill - system not IDLE (current: %s)",
                 this->get_status_string());
        return;
    }

    // Log critical event to PSM
    if (this->psm_) {
        this->psm_->logEvent("WATER_FILL", 0);  // 0 = STARTED
    }

    this->transition_to(LogicStatus::WATER_FILLING);
}

void PlantOSLogic::start_empty_tank() {
    ESP_LOGI(TAG, "Starting tank empty sequence");

    if (this->current_status_ != LogicStatus::IDLE) {
        ESP_LOGW(TAG, "Cannot start empty - system not IDLE (current: %s)",
                 this->get_status_string());
        return;
    }

    // Log critical event to PSM
    if (this->psm_) {
        this->psm_->logEvent("WATER_EMPTY", 0);  // 0 = STARTED
    }

    this->transition_to(LogicStatus::WATER_EMPTYING);
}

void PlantOSLogic::calibrate_ph() {
    ESP_LOGI(TAG, "Starting pH calibration");

    // Stop all routines
    this->turn_all_pumps_off();

    // Transition to calibration state
    this->transition_to(LogicStatus::PH_CALIBRATING);

    // Note: Actual calibration method call would happen here if EZOPH_Sensor interface was available
    // For now, this is a placeholder state
    ESP_LOGW(TAG, "pH calibration requested but EZOPH_Sensor interface not implemented");
}

// ========== State Handler Functions ==========

void PlantOSLogic::handle_idle() {
    // IDLE state - waiting for manual trigger
    // No automatic transitions in this implementation
    // (could add time-based triggers here if needed)
}

void PlantOSLogic::handle_ph_correction_due() {
    // Immediately transition to measuring
    ESP_LOGI(TAG, "pH correction due - starting measurement");
    this->transition_to(LogicStatus::PH_MEASURING);
}

void PlantOSLogic::handle_ph_measuring() {
    uint32_t elapsed = millis() - this->state_start_time_;

    // On entry: turn all pumps off
    if (elapsed < 100) {
        this->turn_all_pumps_off();
        ESP_LOGI(TAG, "pH measuring - all pumps OFF, stabilizing for 5 minutes");
        return;
    }

    // Wait for stabilization period (5 minutes)
    if (elapsed < PH_STABILIZATION_TIME_MS) {
        // Take readings every minute
        uint32_t reading_interval = elapsed / PH_READING_INTERVAL_MS;
        if (reading_interval > this->state_counter_) {
            this->state_counter_ = reading_interval;

            // Read pH sensor
            if (this->ph_sensor_ && this->ph_sensor_->has_state()) {
                float ph = this->ph_sensor_->state;
                this->ph_readings_.push_back(ph);
                ESP_LOGI(TAG, "pH reading %d: %.2f", this->ph_readings_.size(), ph);

                // Check for critical pH
                if (ph < PH_CRITICAL_MIN || ph > PH_CRITICAL_MAX) {
                    if (this->status_logger_) {
                        char buffer[64];
                        snprintf(buffer, sizeof(buffer), "pH out of safe range: %.2f", ph);
                        this->status_logger_->updateAlertStatus("PH_CRITICAL", buffer);
                    }
                }
            }
        }
        return;
    }

    // Stabilization complete - check if we have enough readings
    if (this->ph_readings_.size() < PH_READINGS_REQUIRED) {
        ESP_LOGW(TAG, "Insufficient pH readings (%d < %d) - using available data",
                 this->ph_readings_.size(), PH_READINGS_REQUIRED);
    }

    // Calculate robust average
    if (!this->ph_readings_.empty()) {
        this->ph_current_ = this->calculate_robust_ph_average();
        ESP_LOGI(TAG, "pH stabilization complete - robust average: %.2f", this->ph_current_);
    } else {
        ESP_LOGE(TAG, "No pH readings available!");
        // Clear PSM event and return to IDLE
        if (this->psm_) {
            this->psm_->clearEvent();
        }
        this->transition_to(LogicStatus::IDLE);
        return;
    }

    // Transition to calculating
    this->transition_to(LogicStatus::PH_CALCULATING);
}

void PlantOSLogic::handle_ph_calculating() {
    // Get target pH from calendar
    if (!this->calendar_) {
        ESP_LOGE(TAG, "CalendarManager not available - cannot calculate pH dose");
        if (this->psm_) {
            this->psm_->clearEvent();
        }
        this->transition_to(LogicStatus::IDLE);
        return;
    }

    calendar_manager::DailySchedule schedule = this->calendar_->get_today_schedule();
    float target_ph_min = schedule.target_ph_min;
    float target_ph_max = schedule.target_ph_max;

    ESP_LOGI(TAG, "pH calculation: current=%.2f, target=%.2f-%.2f",
             this->ph_current_, target_ph_min, target_ph_max);

    // Check if pH is within target range
    if (this->ph_current_ >= target_ph_min && this->ph_current_ <= target_ph_max) {
        ESP_LOGI(TAG, "pH within target range - no correction needed");

        // Clear PSM event and return to IDLE
        if (this->psm_) {
            this->psm_->clearEvent();
        }

        // Clear any pH alerts
        if (this->status_logger_) {
            this->status_logger_->clearAlert("PH_CRITICAL");
        }

        this->transition_to(LogicStatus::IDLE);
        return;
    }

    // Calculate required acid duration
    uint32_t acid_duration_ms = this->calculate_acid_duration(this->ph_current_, target_ph_max);

    ESP_LOGI(TAG, "Calculated acid pump duration: %d ms", acid_duration_ms);

    // Check minimum dosing duration
    if (acid_duration_ms < PH_MIN_DOSE_MS) {
        ESP_LOGI(TAG, "Dose too small (<%d ms) - skipping injection", PH_MIN_DOSE_MS);

        // Clear PSM event and return to IDLE
        if (this->psm_) {
            this->psm_->clearEvent();
        }

        this->transition_to(LogicStatus::IDLE);
        return;
    }

    // Check if we've exceeded max attempts
    if (this->ph_attempt_count_ >= MAX_PH_ATTEMPTS) {
        ESP_LOGW(TAG, "Max pH correction attempts (%d) reached - stopping", MAX_PH_ATTEMPTS);

        if (this->status_logger_) {
            this->status_logger_->updateAlertStatus(
                "PH_MAX_ATTEMPTS",
                "Max pH correction attempts reached"
            );
        }

        // Clear PSM event and return to IDLE
        if (this->psm_) {
            this->psm_->clearEvent();
        }

        this->transition_to(LogicStatus::IDLE);
        return;
    }

    // Store duration in state_counter for use in injecting state
    this->state_counter_ = acid_duration_ms;
    this->ph_attempt_count_++;

    ESP_LOGI(TAG, "Starting pH injection (attempt %d/%d)",
             this->ph_attempt_count_, MAX_PH_ATTEMPTS);

    // Transition to injecting
    this->transition_to(LogicStatus::PH_INJECTING);
}

void PlantOSLogic::handle_ph_injecting() {
    uint32_t elapsed = millis() - this->state_start_time_;
    uint32_t dose_duration_ms = this->state_counter_;  // Stored from calculating state
    uint32_t total_duration_ms = dose_duration_ms + 200;  // Add safety margin

    // On entry: activate pumps
    if (elapsed < 100) {
        // Activate air pump for mixing
        if (this->safety_gate_) {
            this->safety_gate_->executeCommand(ACTUATOR_AIR_PUMP, true, 0);
        }

        // Activate acid pump with calculated duration
        if (this->safety_gate_) {
            uint32_t dose_duration_sec = (dose_duration_ms + 999) / 1000;  // Round up to seconds
            bool approved = this->safety_gate_->executeCommand(
                ACTUATOR_ACID_PUMP, true, dose_duration_sec
            );

            if (!approved) {
                ESP_LOGE(TAG, "Acid pump command rejected by SafetyGate!");
                // Turn off air pump and abort
                this->safety_gate_->executeCommand(ACTUATOR_AIR_PUMP, false, 0);
                if (this->psm_) {
                    this->psm_->clearEvent();
                }
                this->transition_to(LogicStatus::IDLE);
                return;
            }
        }

        ESP_LOGI(TAG, "Acid pump ON for %d ms", dose_duration_ms);
        return;
    }

    // Wait for dose duration + safety margin
    if (elapsed < total_duration_ms) {
        return;
    }

    // Turn off acid pump (SafetyGate should handle this automatically,
    // but we explicitly command OFF for safety)
    if (this->safety_gate_) {
        this->safety_gate_->executeCommand(ACTUATOR_ACID_PUMP, false, 0);
    }

    ESP_LOGI(TAG, "Acid injection complete - starting mixing");

    // Transition to mixing
    this->transition_to(LogicStatus::PH_MIXING);
}

void PlantOSLogic::handle_ph_mixing() {
    uint32_t elapsed = millis() - this->state_start_time_;

    // Keep air pump ON for mixing duration (2 minutes)
    if (elapsed < PH_MIXING_TIME_MS) {
        return;
    }

    // Turn off air pump
    if (this->safety_gate_) {
        this->safety_gate_->executeCommand(ACTUATOR_AIR_PUMP, false, 0);
    }

    ESP_LOGI(TAG, "Mixing complete - re-measuring pH");

    // Clear pH readings for next measurement
    this->ph_readings_.clear();

    // Transition back to measuring
    this->transition_to(LogicStatus::PH_MEASURING);
}

void PlantOSLogic::handle_ph_calibrating() {
    // Placeholder state for pH calibration
    // In production, this would call the EZOPH_Sensor calibration method
    // and wait for completion

    ESP_LOGW(TAG, "pH calibration state active - manual reset required");

    // This state requires manual intervention to exit
    // (could add timeout or completion detection here)
}

void PlantOSLogic::handle_feeding_due() {
    // Get feeding schedule from calendar
    if (!this->calendar_) {
        ESP_LOGE(TAG, "CalendarManager not available - cannot start feeding");
        if (this->psm_) {
            this->psm_->clearEvent();
        }
        this->transition_to(LogicStatus::IDLE);
        return;
    }

    calendar_manager::DailySchedule schedule = this->calendar_->get_today_schedule();

    ESP_LOGI(TAG, "Feeding schedule: A=%dms, B=%dms, C=%dms",
             schedule.nutrient_A_duration_ms,
             schedule.nutrient_B_duration_ms,
             schedule.nutrient_C_duration_ms);

    // Store durations in state_counter for use in injecting state
    // (For simplicity, we'll start with nutrient A)
    this->state_counter_ = 0;  // Use as pump index (0=A, 1=B, 2=C)

    this->transition_to(LogicStatus::FEEDING_INJECTING);
}

void PlantOSLogic::handle_feeding_injecting() {
    uint32_t elapsed = millis() - this->state_start_time_;

    // Get feeding schedule
    calendar_manager::DailySchedule schedule = this->calendar_->get_today_schedule();

    // Determine which pump to activate based on state_counter
    const char* pump_name = nullptr;
    uint32_t duration_ms = 0;

    if (this->state_counter_ == 0) {
        pump_name = ACTUATOR_NUTRIENT_A;
        duration_ms = schedule.nutrient_A_duration_ms;
    } else if (this->state_counter_ == 1) {
        pump_name = ACTUATOR_NUTRIENT_B;
        duration_ms = schedule.nutrient_B_duration_ms;
    } else if (this->state_counter_ == 2) {
        pump_name = ACTUATOR_NUTRIENT_C;
        duration_ms = schedule.nutrient_C_duration_ms;
    } else {
        // All pumps done
        ESP_LOGI(TAG, "Feeding complete");

        // Clear PSM event
        if (this->psm_) {
            this->psm_->clearEvent();
        }

        this->transition_to(LogicStatus::IDLE);
        return;
    }

    // On entry for this pump: activate it
    if (elapsed < 100) {
        if (duration_ms > 0) {
            if (this->safety_gate_) {
                uint32_t duration_sec = (duration_ms + 999) / 1000;  // Round up
                bool approved = this->safety_gate_->executeCommand(pump_name, true, duration_sec);

                if (!approved) {
                    ESP_LOGE(TAG, "%s command rejected by SafetyGate!", pump_name);
                    // Abort feeding
                    if (this->psm_) {
                        this->psm_->clearEvent();
                    }
                    this->transition_to(LogicStatus::IDLE);
                    return;
                }
            }

            ESP_LOGI(TAG, "%s ON for %d ms", pump_name, duration_ms);
        } else {
            ESP_LOGI(TAG, "%s duration is 0 - skipping", pump_name);
            // Immediately move to next pump
            this->state_counter_++;
            this->state_start_time_ = millis();
        }
        return;
    }

    // Wait for duration + safety margin
    if (elapsed < duration_ms + 200) {
        return;
    }

    // Turn off pump
    if (this->safety_gate_) {
        this->safety_gate_->executeCommand(pump_name, false, 0);
    }

    ESP_LOGI(TAG, "%s complete", pump_name);

    // Move to next pump
    this->state_counter_++;
    this->state_start_time_ = millis();
}

void PlantOSLogic::handle_water_management_due() {
    // Placeholder - could add logic to determine fill vs empty
    ESP_LOGW(TAG, "Water management state - not implemented");

    if (this->psm_) {
        this->psm_->clearEvent();
    }

    this->transition_to(LogicStatus::IDLE);
}

void PlantOSLogic::handle_water_filling() {
    uint32_t elapsed = millis() - this->state_start_time_;

    // Example: Fill for 30 seconds
    static constexpr uint32_t FILL_DURATION_MS = 30000;

    // On entry: open water valve
    if (elapsed < 100) {
        if (this->safety_gate_) {
            this->safety_gate_->executeCommand(ACTUATOR_WATER_VALVE, true, 30);
        }
        ESP_LOGI(TAG, "Water valve OPEN for %d seconds", FILL_DURATION_MS / 1000);
        return;
    }

    // Wait for fill duration
    if (elapsed < FILL_DURATION_MS + 200) {
        return;
    }

    // Close valve
    if (this->safety_gate_) {
        this->safety_gate_->executeCommand(ACTUATOR_WATER_VALVE, false, 0);
    }

    ESP_LOGI(TAG, "Water fill complete");

    // Clear PSM event
    if (this->psm_) {
        this->psm_->clearEvent();
    }

    this->transition_to(LogicStatus::IDLE);
}

void PlantOSLogic::handle_water_emptying() {
    uint32_t elapsed = millis() - this->state_start_time_;

    // Example: Empty for 30 seconds
    static constexpr uint32_t EMPTY_DURATION_MS = 30000;

    // On entry: activate wastewater pump
    if (elapsed < 100) {
        if (this->safety_gate_) {
            this->safety_gate_->executeCommand(ACTUATOR_WASTEWATER_PUMP, true, 30);
        }
        ESP_LOGI(TAG, "Wastewater pump ON for %d seconds", EMPTY_DURATION_MS / 1000);
        return;
    }

    // Wait for empty duration
    if (elapsed < EMPTY_DURATION_MS + 200) {
        return;
    }

    // Turn off pump
    if (this->safety_gate_) {
        this->safety_gate_->executeCommand(ACTUATOR_WASTEWATER_PUMP, false, 0);
    }

    ESP_LOGI(TAG, "Water empty complete");

    // Clear PSM event
    if (this->psm_) {
        this->psm_->clearEvent();
    }

    this->transition_to(LogicStatus::IDLE);
}

// ========== Helper Functions ==========

void PlantOSLogic::transition_to(LogicStatus new_status) {
    LogicStatus old_status = this->current_status_;

    if (old_status != new_status) {
        ESP_LOGI(TAG, "State transition: %s -> %s",
                 this->get_status_string(),
                 this->get_status_string()); // Note: This will show new state after assignment

        this->current_status_ = new_status;
        this->state_start_time_ = millis();
        this->state_counter_ = 0;

        // Publish state change
        this->publish_state();

        // Update status logger
        if (this->status_logger_) {
            this->status_logger_->updateStatus(
                this->ph_current_,
                this->get_status_string()
            );
        }
    }
}

void PlantOSLogic::turn_all_pumps_off() {
    if (!this->safety_gate_) {
        return;
    }

    ESP_LOGI(TAG, "Turning all pumps OFF");

    this->safety_gate_->executeCommand(ACTUATOR_ACID_PUMP, false, 0);
    this->safety_gate_->executeCommand(ACTUATOR_NUTRIENT_A, false, 0);
    this->safety_gate_->executeCommand(ACTUATOR_NUTRIENT_B, false, 0);
    this->safety_gate_->executeCommand(ACTUATOR_NUTRIENT_C, false, 0);
    this->safety_gate_->executeCommand(ACTUATOR_WATER_VALVE, false, 0);
    this->safety_gate_->executeCommand(ACTUATOR_WASTEWATER_PUMP, false, 0);
    this->safety_gate_->executeCommand(ACTUATOR_AIR_PUMP, false, 0);
}

uint32_t PlantOSLogic::calculate_acid_duration(float current_ph, float target_ph_max) {
    // Simple linear calculation: 1 second per 0.1 pH unit
    // This is a placeholder - production would use calibrated dosing curve
    float ph_diff = current_ph - target_ph_max;

    if (ph_diff <= 0.0f) {
        return 0;  // pH already below target
    }

    // Calculate duration: 1000ms per 0.1 pH unit
    uint32_t duration_ms = static_cast<uint32_t>(ph_diff * 10.0f * 1000.0f);

    // Cap at reasonable maximum (30 seconds)
    if (duration_ms > 30000) {
        duration_ms = 30000;
    }

    return duration_ms;
}

float PlantOSLogic::calculate_robust_ph_average() {
    if (this->ph_readings_.empty()) {
        return 0.0f;
    }

    // Sort readings
    std::vector<float> sorted = this->ph_readings_;
    std::sort(sorted.begin(), sorted.end());

    // Reject 10% from each end (robust averaging)
    size_t reject_count = sorted.size() / 10;
    if (reject_count == 0 && sorted.size() > 2) {
        reject_count = 1;
    }

    // Calculate average of middle values
    float sum = 0.0f;
    size_t count = 0;

    for (size_t i = reject_count; i < sorted.size() - reject_count; i++) {
        sum += sorted[i];
        count++;
    }

    return count > 0 ? sum / count : sorted[sorted.size() / 2];
}

bool PlantOSLogic::is_ph_stable() {
    if (this->ph_readings_.size() < 2) {
        return false;
    }

    // Check if last few readings are within stability threshold
    size_t check_count = std::min(size_t(3), this->ph_readings_.size());
    float min_val = this->ph_readings_[this->ph_readings_.size() - 1];
    float max_val = min_val;

    for (size_t i = 1; i < check_count; i++) {
        float val = this->ph_readings_[this->ph_readings_.size() - 1 - i];
        min_val = std::min(min_val, val);
        max_val = std::max(max_val, val);
    }

    return (max_val - min_val) <= PH_STABILITY_THRESHOLD;
}

void PlantOSLogic::publish_state() {
    if (this->state_text_) {
        this->state_text_->publish_state(this->get_status_string());
    }
}

const char* PlantOSLogic::get_status_string() const {
    switch (this->current_status_) {
        case LogicStatus::IDLE:
            return "IDLE";
        case LogicStatus::PH_CORRECTION_DUE:
            return "PH_CORRECTION_DUE";
        case LogicStatus::PH_MEASURING:
            return "PH_MEASURING";
        case LogicStatus::PH_CALCULATING:
            return "PH_CALCULATING";
        case LogicStatus::PH_INJECTING:
            return "PH_INJECTING";
        case LogicStatus::PH_MIXING:
            return "PH_MIXING";
        case LogicStatus::PH_CALIBRATING:
            return "PH_CALIBRATING";
        case LogicStatus::FEEDING_DUE:
            return "FEEDING_DUE";
        case LogicStatus::FEEDING_INJECTING:
            return "FEEDING_INJECTING";
        case LogicStatus::WATER_MANAGEMENT_DUE:
            return "WATER_MANAGEMENT_DUE";
        case LogicStatus::WATER_FILLING:
            return "WATER_FILLING";
        case LogicStatus::WATER_EMPTYING:
            return "WATER_EMPTYING";
        default:
            return "UNKNOWN";
    }
}

} // namespace plantos_logic
} // namespace esphome
