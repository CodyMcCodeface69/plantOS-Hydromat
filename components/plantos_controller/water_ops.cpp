// Water operations: tank fill, tank empty and the fill step of the feed / reservoir change sequence.
// Part of PlantOSController (declared in controller.h). FSMINFO.md is authoritative for FSM behavior.
#include "controller.h"
#include "esphome/components/plantos_hal/hal.h"
#include "esphome/components/actuator_safety_gate/ActuatorSafetyGate.h"
#include "esphome/components/persistent_state_manager/persistent_state_manager.h"
#include "esphome/components/calendar_manager/CalendarManager.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/alert_service/alert_service.h"
#include <algorithm>
#include <cmath>

namespace plantos_controller {

void PlantOSController::handleWaterFilling() {
    // Blue solid LED handled by LedBehaviorSystem
    uint32_t elapsed = getStateElapsed();

    // FILL TIMEOUT: 10 minutes - safety backup if the HIGH sensor never triggers
    // (CONFIGURE ACCORDING TO: tank volume and Mag Valve flowspeed)
    static constexpr uint32_t FILL_DURATION_MS = 600000;

    // ENTRY: Log and activate water valve (only once per state entry)
    static bool valve_command_sent = false;

    if (elapsed < 100 && !valve_command_sent) {
        ESP_LOGI(TAG, "[WATER_FILLING] Starting tank fill sequence");

        // SAFETY: Abort if water level sensors are unavailable - filling blind
        // on a timeout with no level feedback risks tank overflow
        if (!hal_ || !hal_->hasWaterLevelSensors()) {
            ESP_LOGE(TAG, "[WATER_FILLING] SAFETY ABORT: water level sensors unavailable - refusing blind fill");

            if (ENHANCED_ERROR_HANDLING_ENABLED) {
                status_logger_.updateAlertWithContext(
                    "HARDWARE_WATER_SENSORS_MISSING",
                    "Water level sensors unavailable - fill aborted",
                    "hasWaterLevelSensors() returned false when starting WATER_FILLING",
                    "Check water level sensor configuration in plantOS.yaml and HAL wiring (HIGH=GPIO17, LOW=GPIO23, EMPTY=GPIO22)",
                    "Water filling refused to prevent overflow",
                    "Fix sensor configuration before filling the tank.",
                    0
                );
            }

            // Clear pending flags and PSM event - fill never started
            auto_ph_correction_pending_ = false;
            if (psm_) {
                psm_->clearEvent();
            }

            valve_command_sent = false;
            transitionTo(ControllerState::IDLE);
            return;
        }

        // Request water valve open; ASG validates against its 600s max duration
        // and force-closes the valve as a hardware-level backstop
        if (safety_gate_) {
            bool approved = safety_gate_->executeCommand(WATER_VALVE, true, FILL_DURATION_MS / 1000);

            if (!approved) {
                ESP_LOGE(TAG, "[WATER_FILLING] SafetyGate rejected WaterValve command");

                // Set comprehensive alert
                if (ENHANCED_ERROR_HANDLING_ENABLED) {
                    status_logger_.updateAlertWithContext(
                        "PUMP_REJECTION_WATER_VALVE",
                        "Water valve rejected by SafetyGate",
                        "SafetyGate rejected water valve command",
                        "Check water valve wiring, verify valve not stuck, check SafetyGate max duration config",
                        "Water filling operation aborted",
                        "Manual intervention required. Check valve hardware.",
                        0
                    );
                }

                // Clear PSM event - fill aborted
                if (psm_) {
                    psm_->clearEvent();
                }

                valve_command_sent = false;  // Reset for next attempt
                transitionTo(ControllerState::IDLE);
                return;
            }
        }

        ESP_LOGI(TAG, "[WATER_FILLING] Water valve OPENED");
        valve_command_sent = true;  // Mark as sent
        return;
    }

    // MONITOR: Check water level sensors (if available)
    if (hal_->hasWaterLevelSensors()) {
        bool high_sensor = hal_->readWaterLevelHigh();
        bool low_sensor = hal_->readWaterLevelLow();

        // SUCCESS: High level reached
        if (high_sensor) {
            ESP_LOGI(TAG, "[WATER_FILLING] HIGH sensor triggered - tank full");

            // Close water valve
            if (safety_gate_) {
                safety_gate_->executeCommand(WATER_VALVE, false, 0);
            }

            // Resolve any active water-related alerts
            if (ENHANCED_ERROR_HANDLING_ENABLED) {
                status_logger_.resolveAlert("PUMP_REJECTION_WATER_VALVE");
                status_logger_.resolveAlert("HARDWARE_WATER_SENSOR_HIGH");
            }

            // Clear PSM event
            if (psm_) {
                psm_->clearEvent();
            }

            valve_command_sent = false;  // Reset for next fill cycle
            ESP_LOGI(TAG, "[WATER_FILLING] Fill complete");

            // Post-fill sequence: queue EC check and pH correction, let IDLE dispatch them
            ESP_LOGI(TAG, "[WATER_FILLING] Post-fill: queuing EC check → pH correction sequence");
            auto_ec_check_pending_ = true;
            auto_ph_correction_pending_ = true;
            ph_post_fill_stabilize_ = true;  // Extended 10-min pH stabilization after fill
            ec_cycle_water_filled_ = false;  // Reset EC cycle guard
            ph_cycle_water_filled_ = false;  // Reset pH cycle guard

            // Reset EC cycle state for clean measurement
            ec_attempt_count_ = 0;
            ec_total_ml_per_L_ = 0.0f;

            transitionTo(ControllerState::IDLE);
            return;
        }

        // MONITOR: Log progress when LOW sensor triggered
        static bool low_sensor_logged = false;
        if (low_sensor && !low_sensor_logged) {
            ESP_LOGI(TAG, "[WATER_FILLING] LOW sensor triggered - filling in progress");
            low_sensor_logged = true;
        }
        if (!low_sensor) {
            low_sensor_logged = false;  // Reset for next fill cycle
        }
    } else {
        // SAFETY: Sensors disappeared mid-fill (should be unreachable - entry
        // guard already checked) - close valve immediately and abort
        ESP_LOGE(TAG, "[WATER_FILLING] SAFETY ABORT: water level sensors lost during fill - closing valve");

        if (safety_gate_) {
            safety_gate_->executeCommand(WATER_VALVE, false, 0);
        }
        if (psm_) {
            psm_->clearEvent();
        }

        auto_ph_correction_pending_ = false;
        valve_command_sent = false;
        transitionTo(ControllerState::IDLE);
        return;
    }

    // TIMEOUT: Safety backup - 10 minute max fill time
    if (elapsed >= FILL_DURATION_MS) {
        ESP_LOGW(TAG, "[WATER_FILLING] 10-minute timeout reached - closing valve");

        // Close water valve
        if (safety_gate_) {
            safety_gate_->executeCommand(WATER_VALVE, false, 0);
        }

        // Clear PSM event
        if (psm_) {
            psm_->clearEvent();
        }

        // Alert: sensors are present (guaranteed by entry guard) but HIGH never triggered
        ESP_LOGE(TAG, "[WATER_FILLING] HIGH sensor never triggered - possible sensor failure or low water pressure");

        // Set comprehensive alert for sensor failure
        if (ENHANCED_ERROR_HANDLING_ENABLED) {
            status_logger_.updateAlertWithContext(
                "HARDWARE_WATER_SENSOR_HIGH",
                "HIGH water level sensor never triggered during fill",
                "Sensor did not trigger after 10-minute timeout - possible sensor failure or disconnection",
                "Check HIGH water sensor wiring (GPIO17), verify sensor power, test sensor with multimeter",
                "Water filling completed via timeout (10 min) instead of sensor trigger",
                "Tank may be under-filled or sensor may be faulty. Manual verification recommended.",
                0
            );
        }

        valve_command_sent = false;  // Reset for next fill cycle
        ESP_LOGI(TAG, "[WATER_FILLING] Timeout complete");

        // Post-fill sequence: queue EC check and pH correction, let IDLE dispatch them
        ESP_LOGI(TAG, "[WATER_FILLING] Post-fill: queuing EC check → pH correction sequence");
        auto_ec_check_pending_ = true;
        auto_ph_correction_pending_ = true;
        ph_post_fill_stabilize_ = true;  // Extended 10-min pH stabilization after fill
        ec_cycle_water_filled_ = false;  // Reset EC cycle guard
        ph_cycle_water_filled_ = false;  // Reset pH cycle guard

        // Reset EC cycle state for clean measurement
        ec_attempt_count_ = 0;
        ec_total_ml_per_L_ = 0.0f;

        transitionTo(ControllerState::IDLE);
        return;
    }
}

void PlantOSController::handleWaterEmptying() {
    // Purple pulse LED handled by LedBehaviorSystem
    uint32_t elapsed = getStateElapsed();

    // Empty duration: 300 seconds (fallback if sensors unavailable)
    static constexpr uint32_t EMPTY_DURATION_MS = 300000;

    // ENTRY: Log and activate wastewater pump (only once per state entry)
    static bool pump_command_sent = false;

    // Shelly integration switched off mid-drain: the HAL already queued WastewaterPump OFF.
    // Abort the drain (and a running reservoir change) to IDLE.
    if (!hal_->isShellyEnabled()) {
        ESP_LOGE(TAG, "[WATER_EMPTYING] Shelly integration disabled - aborting drain");
        if (safety_gate_) {
            safety_gate_->executeCommand(WASTEWATER_PUMP, false, 0);
        }
        if (psm_) {
            psm_->clearEvent();
        }
        pump_command_sent = false;
        auto_ph_correction_pending_ = false;
        transitionTo(ControllerState::IDLE);
        return;
    }

    if (elapsed < 100 && !pump_command_sent) {
        ESP_LOGI(TAG, "[WATER_EMPTYING] Starting tank drain sequence");

        // Check EMPTY sensor before starting (prevent dry-pump immediately)
        if (hal_->hasWaterLevelSensors() && !hal_->readWaterLevelEmpty()) {
            ESP_LOGW(TAG, "[WATER_EMPTYING] EMPTY sensor already OFF - tank already empty");

            // Clear PSM event
            if (psm_) {
                psm_->clearEvent();
            }

            pump_command_sent = false;  // Reset for next attempt

            // Check if part of reservoir change sequence
            if (auto_ph_correction_pending_) {
                ESP_LOGI(TAG, "[WATER_EMPTYING] Tank already empty - proceeding to fill phase");
                transitionTo(ControllerState::FEED_FILLING);
            } else {
                transitionTo(ControllerState::IDLE);
            }
            return;
        }

        // Request wastewater pump on (300s max duration as safety backup)
        if (safety_gate_) {
            bool approved = safety_gate_->executeCommand(WASTEWATER_PUMP, true, 300);  // 300 seconds max

            if (!approved) {
                ESP_LOGE(TAG, "[WATER_EMPTYING] SafetyGate rejected WastewaterPump command");

                // Set comprehensive alert
                if (ENHANCED_ERROR_HANDLING_ENABLED) {
                    status_logger_.updateAlertWithContext(
                        "PUMP_REJECTION_WASTEWATER",
                        "Wastewater pump rejected by SafetyGate",
                        "SafetyGate rejected wastewater pump command",
                        "Check wastewater pump wiring, verify pump not stuck, check SafetyGate max duration config",
                        "Water emptying operation aborted",
                        "Manual intervention required. Check pump hardware.",
                        0
                    );
                }

                // Clear PSM event - drain aborted
                if (psm_) {
                    psm_->clearEvent();
                }

                pump_command_sent = false;  // Reset for next attempt

                // Check if part of reservoir change sequence
                if (auto_ph_correction_pending_) {
                    ESP_LOGW(TAG, "[WATER_EMPTYING] Drain rejected - aborting reservoir change");
                    auto_ph_correction_pending_ = false;  // Clear flag - sequence aborted
                }
                transitionTo(ControllerState::IDLE);
                return;
            }
        }

        ESP_LOGI(TAG, "[WATER_EMPTYING] Wastewater pump ACTIVATED");
        pump_command_sent = true;  // Mark as sent
        return;
    }

    // MONITOR: Check water level sensors (if available)
    if (hal_->hasWaterLevelSensors()) {
        bool empty_sensor = hal_->readWaterLevelEmpty();
        bool low_sensor = hal_->readWaterLevelLow();

        // SUCCESS: EMPTY sensor OFF (tank below minimum safe level)
        if (!empty_sensor) {
            ESP_LOGI(TAG, "[WATER_EMPTYING] EMPTY sensor OFF - minimum safe level reached");

            // Stop wastewater pump
            if (safety_gate_) {
                safety_gate_->executeCommand(WASTEWATER_PUMP, false, 0);
            }

            // Resolve any active water-related alerts
            if (ENHANCED_ERROR_HANDLING_ENABLED) {
                status_logger_.resolveAlert("PUMP_REJECTION_WASTEWATER");
                status_logger_.resolveAlert("HARDWARE_WATER_SENSOR_EMPTY");
            }

            // Clear PSM event
            if (psm_) {
                psm_->clearEvent();
            }

            pump_command_sent = false;  // Reset for next drain cycle

            // Check if part of reservoir change sequence
            if (auto_ph_correction_pending_) {
                ESP_LOGI(TAG, "[WATER_EMPTYING] Drain complete - proceeding to fill phase");
                transitionTo(ControllerState::FEED_FILLING);
            } else {
                ESP_LOGI(TAG, "[WATER_EMPTYING] Drain complete - returning to IDLE");
                transitionTo(ControllerState::IDLE);
            }
            return;
        }

        // MONITOR: Log progress every 5 seconds
        static uint32_t last_progress_log = 0;
        if (elapsed - last_progress_log >= 5000) {
            ESP_LOGI(TAG, "[WATER_EMPTYING] Draining... LOW=%s, EMPTY=%s",
                     low_sensor ? "ON" : "OFF",
                     empty_sensor ? "ON" : "OFF");
            last_progress_log = elapsed;
        }
    } else {
        // FALLBACK: No sensors available - use time-based limit
        static bool no_sensor_warning_logged = false;
        if (!no_sensor_warning_logged) {
            ESP_LOGW(TAG, "[WATER_EMPTYING] Water level sensors not available - using 300s timeout");
            no_sensor_warning_logged = true;
        }
    }

    // TIMEOUT: Safety backup - 300s max drain time
    if (elapsed >= EMPTY_DURATION_MS) {
        ESP_LOGW(TAG, "[WATER_EMPTYING] 300s timeout reached - stopping pump");

        // Stop wastewater pump
        if (safety_gate_) {
            safety_gate_->executeCommand(WASTEWATER_PUMP, false, 0);
        }

        // Clear PSM event
        if (psm_) {
            psm_->clearEvent();
        }

        // Alert if sensors were expected but didn't trigger
        if (hal_->hasWaterLevelSensors()) {
            ESP_LOGE(TAG, "[WATER_EMPTYING] EMPTY sensor never cleared - possible sensor failure or clog");

            // Set comprehensive alert for sensor failure
            if (ENHANCED_ERROR_HANDLING_ENABLED) {
                status_logger_.updateAlertWithContext(
                    "HARDWARE_WATER_SENSOR_EMPTY",
                    "EMPTY water level sensor never cleared during drain",
                    "Sensor did not clear after 300s timeout - possible sensor failure, disconnection, or clog",
                    "Check EMPTY water sensor wiring (GPIO22), verify sensor power, test sensor, check for clogs",
                    "Water emptying completed via timeout (300s) instead of sensor trigger",
                    "Tank may not be fully drained or sensor may be faulty. Manual verification recommended.",
                    0
                );
            }
        }

        pump_command_sent = false;  // Reset for next drain cycle

        // Check if part of reservoir change sequence
        if (auto_ph_correction_pending_) {
            ESP_LOGI(TAG, "[WATER_EMPTYING] Timeout complete - proceeding to fill phase");
            transitionTo(ControllerState::FEED_FILLING);
        } else {
            ESP_LOGI(TAG, "[WATER_EMPTYING] Timeout complete - returning to IDLE");
            transitionTo(ControllerState::IDLE);
        }
        return;
    }
}

void PlantOSController::handleFeedFilling() {
    // Blue solid LED handled by LedBehaviorSystem (same as WATER_FILLING)
    // This is the first phase of the Feed operation

    uint32_t elapsed = getStateElapsed();

    // Calculate max fill duration based on tank volume and valve flow rate
    // Formula: duration = (tank_volume_mL / valve_flow_rate_mL_s) × 1.2 (20% safety margin)
    uint32_t max_fill_duration_ms = 600000;  // Default fallback: 10 minutes
    uint32_t max_fill_duration_sec = 600;    // For SafetyGate command

    if (hal_) {
        // Select tank volume based on operation context:
        // - Normal feed: fills from LOW to HIGH → use delta volume
        // - Reservoir change: fills from EMPTY to HIGH → use total volume
        float tank_volume_L = is_reservoir_change_
            ? hal_->getTotalTankVolume()
            : hal_->getTankVolumeDelta();
        float tank_volume_mL = tank_volume_L * 1000.0f;

        // Use valveflow() to calculate fill duration for the appropriate tank volume
        float fill_duration_sec = hal_->valveflow(tank_volume_mL);

        // Add 20% safety margin
        fill_duration_sec *= 1.2f;

        max_fill_duration_ms = static_cast<uint32_t>(fill_duration_sec * 1000.0f);
        max_fill_duration_sec = static_cast<uint32_t>(fill_duration_sec);

        ESP_LOGI(TAG, "[FEED_FILLING] Calculated max fill time: %.1f sec (%.1fL %s volume with 20%% margin)",
                 fill_duration_sec, tank_volume_L,
                 is_reservoir_change_ ? "TOTAL" : "DELTA");
    } else {
        ESP_LOGW(TAG, "[FEED_FILLING] HAL not available - using fallback timeout of 10 min");
    }

    // ENTRY: Log and activate water valve (only once!)
    if (!state_entry_executed_) {
        state_entry_executed_ = true;
        ESP_LOGI(TAG, "[FEED_FILLING] Starting tank fill before nutrient dosing");

        // SAFETY: Abort if water level sensors are unavailable - filling blind
        // on a timeout with no level feedback risks tank overflow
        if (!hal_ || !hal_->hasWaterLevelSensors()) {
            ESP_LOGE(TAG, "[FEED_FILLING] SAFETY ABORT: water level sensors unavailable - refusing blind fill");

            if (ENHANCED_ERROR_HANDLING_ENABLED) {
                status_logger_.updateAlertWithContext(
                    "HARDWARE_WATER_SENSORS_MISSING",
                    "Water level sensors unavailable - fill aborted",
                    "hasWaterLevelSensors() returned false when starting FEED_FILLING",
                    "Check water level sensor configuration in plantOS.yaml and HAL wiring (HIGH=GPIO17, LOW=GPIO23, EMPTY=GPIO22)",
                    "Feed sequence aborted before filling to prevent overflow",
                    "Fix sensor configuration before feeding.",
                    0
                );
            }

            // Clear flags
            auto_ph_correction_pending_ = false;

            // Clear PSM event
            if (psm_) {
                psm_->clearEvent();
            }

            // Abort sequence
            transitionTo(ControllerState::IDLE);
            return;
        }

        // Safety check - verify tank is EMPTY before filling (all 3 sensors OFF)
        {
            bool high_sensor = hal_->readWaterLevelHigh();
            bool low_sensor = hal_->readWaterLevelLow();
            bool empty_sensor = hal_->readWaterLevelEmpty();

            // Tank must be empty: All sensors OFF
            bool tank_is_empty = (!high_sensor && !low_sensor && !empty_sensor);

            if (!tank_is_empty) {
                ESP_LOGE(TAG, "[FEED_FILLING] SAFETY ABORT: Tank not empty before fill");
                ESP_LOGE(TAG, "[FEED_FILLING] Sensors: HIGH=%s, LOW=%s, EMPTY=%s",
                         high_sensor ? "ON" : "OFF",
                         low_sensor ? "ON" : "OFF",
                         empty_sensor ? "ON" : "OFF");
                ESP_LOGE(TAG, "[FEED_FILLING] Tank must be completely drained before feeding");

                // Clear flags
                auto_ph_correction_pending_ = false;

                // Clear PSM event
                if (psm_) {
                    psm_->clearEvent();
                }

                // Abort sequence
                transitionTo(ControllerState::IDLE);
                return;
            }

            ESP_LOGI(TAG, "[FEED_FILLING] Safety check passed - tank empty (all sensors OFF)");
        }

        // Request water valve open with calculated max duration
        if (safety_gate_) {
            bool approved = safety_gate_->executeCommand(WATER_VALVE, true, max_fill_duration_sec);

            if (!approved) {
                ESP_LOGE(TAG, "[FEED_FILLING] SafetyGate rejected WaterValve command");

                // Clear flags
                auto_ph_correction_pending_ = false;

                // Clear PSM event
                if (psm_) {
                    psm_->clearEvent();
                }

                transitionTo(ControllerState::IDLE);
                return;
            }
        }

        ESP_LOGI(TAG, "[FEED_FILLING] Water valve OPENED (max duration: %d sec)", max_fill_duration_sec);
        return;
    }

    // MONITOR: Check water level sensors (if available)
    if (hal_->hasWaterLevelSensors()) {
        bool high_sensor = hal_->readWaterLevelHigh();
        bool low_sensor = hal_->readWaterLevelLow();

        // SUCCESS: Tank full when BOTH sensors are ON
        if (high_sensor && low_sensor) {
            ESP_LOGI(TAG, "[FEED_FILLING] BOTH sensors ON - tank full");
            ESP_LOGI(TAG, "[FEED_FILLING] Water level: HIGH=ON, LOW=ON");

            // Close water valve
            if (safety_gate_) {
                safety_gate_->executeCommand(WATER_VALVE, false, 0);
            }

            ESP_LOGI(TAG, "[FEED_FILLING] Fill complete - proceeding to nutrient dosing");

            // Transition to FEEDING (nutrients)
            // Note: auto_ph_correction_pending_ flag is already set
            transitionTo(ControllerState::FEEDING);
            return;
        }

        // LOG: Progress update every 5 seconds
        static uint32_t last_progress_log = 0;
        if (elapsed - last_progress_log >= 5000) {
            ESP_LOGI(TAG, "[FEED_FILLING] Filling in progress... HIGH=%s, LOW=%s",
                     high_sensor ? "ON" : "OFF", low_sensor ? "ON" : "OFF");
            last_progress_log = elapsed;
        }
    }

    // TIMEOUT: Safety backup - calculated max fill time
    if (elapsed >= max_fill_duration_ms) {
        ESP_LOGW(TAG, "[FEED_FILLING] Timeout reached (%d sec) - closing valve", max_fill_duration_sec);

        // Close water valve
        if (safety_gate_) {
            safety_gate_->executeCommand(WATER_VALVE, false, 0);
        }

        // Alert if sensors were expected but didn't trigger
        if (hal_->hasWaterLevelSensors()) {
            ESP_LOGE(TAG, "[FEED_FILLING] BOTH sensors never triggered - possible sensor failure or low water pressure");
        }

        ESP_LOGI(TAG, "[FEED_FILLING] Proceeding to nutrient dosing despite timeout");
        transitionTo(ControllerState::FEEDING);
        return;
    }
}
} // namespace plantos_controller
