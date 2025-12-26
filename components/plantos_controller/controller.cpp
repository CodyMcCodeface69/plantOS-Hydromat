#include "controller.h"
#include "esphome/components/plantos_hal/hal.h"
#include "esphome/components/actuator_safety_gate/ActuatorSafetyGate.h"
#include "esphome/components/persistent_state_manager/persistent_state_manager.h"
#include "esphome/components/ezo_ph_uart/ezo_ph_uart.h"
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

    // Initialize status logger
    status_logger_.begin();

    // Verify critical dependencies
    if (!hal_) {
        ESP_LOGE(TAG, "HAL not configured - Controller cannot function!");
        return;
    }

    if (!safety_gate_) {
        ESP_LOGW(TAG, "SafetyGate not configured - Actuator control disabled");
    }

    if (psm_) {
        ESP_LOGI(TAG, "PSM configured - State persistence enabled");
    } else {
        ESP_LOGW(TAG, "PSM not configured - State persistence disabled");
    }

    // Always start in INIT state for boot sequence
    ESP_LOGI(TAG, "Controller initialized - Starting INIT boot sequence");
    transitionTo(ControllerState::INIT);
}

void PlantOSController::loop() {
    if (!hal_) {
        return; // Cannot operate without HAL
    }

    // Update LED behavior for current state
    uint32_t elapsed = getStateElapsed();
    led_behaviors_->update(current_state_, elapsed, hal_);

    // Check if we should print periodic status report
    if (status_logger_.shouldPrintStatusReport()) {
        // Update status logger with current pH before printing
        if (hal_->hasPhValue()) {
            status_logger_.updateStatus(hal_->readPH(), "");
        }

        // Update UART hardware status (EZO pH sensor)
        if (ph_sensor_) {
            std::vector<UARTDeviceInfo> uartDevices;
            std::string status = "";

            // Check if sensor is ready
            bool isReady = ph_sensor_->is_sensor_ready();

            // Add calibration status if available
            // Note: Calibration query would require async call, so we just show ready status
            if (isReady) {
                status = "Ready";
            } else {
                status = "Not responding";
            }

            uartDevices.push_back(UARTDeviceInfo(
                "EZO pH Sensor",
                "TX=GPIO4, RX=GPIO5",
                isReady,
                true,  // critical
                status
            ));

            status_logger_.updateUARTHardwareStatus(uartDevices);
        }

        // Update PSM event info in status logger
        if (psm_) {
            if (psm_->hasEvent()) {
                esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
                status_logger_.updatePSMEvent(event.eventID, event.status, psm_->getEventAge());
            } else {
                // No event - clear PSM info in status logger
                status_logger_.updatePSMEvent("", 0, -1);
            }
        }

        status_logger_.logStatus();
    }

    // Call state-specific handler
    switch (current_state_) {
        case ControllerState::INIT:
            handleInit();
            break;

        case ControllerState::IDLE:
            handleIdle();
            break;

        case ControllerState::SHUTDOWN:
            handleShutdown();
            break;

        case ControllerState::PAUSE:
            handlePause();
            break;

        case ControllerState::ERROR:
            handleError();
            break;

        case ControllerState::PH_PROCESSING:
            handlePhProcessing();
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

    uint32_t elapsed = getStateElapsed();

    // On first entry (elapsed < 100ms): Check PSM for persisted states
    // We check here (not in setup()) because PSM might initialize after controller
    if (elapsed < 100 && !boot_restore_pending_) {
        if (psm_) {
            ESP_LOGI(TAG, "Checking PSM for persisted state...");

            // Check if we were in SHUTDOWN or PAUSE state before power loss
            if (psm_->hasEvent()) {
                esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
                std::string eventID(event.eventID);

                // Check if event ID is non-empty (not a cleared event)
                if (!eventID.empty()) {
                    if (eventID == "STATE_SHUTDOWN") {
                        ESP_LOGW(TAG, "RECOVERY: SHUTDOWN state found in PSM (age: %lld sec) - will restore after INIT",
                                 psm_->getEventAge());
                        boot_restore_state_ = ControllerState::SHUTDOWN;
                        boot_restore_pending_ = true;
                    } else if (eventID == "STATE_PAUSE") {
                        ESP_LOGW(TAG, "RECOVERY: PAUSE state found in PSM (age: %lld sec) - will restore after INIT",
                                 psm_->getEventAge());
                        boot_restore_state_ = ControllerState::PAUSE;
                        boot_restore_pending_ = true;
                    } else {
                        // Some other operation was interrupted - log warning
                        ESP_LOGW(TAG, "RECOVERY: Interrupted operation found: %s (status: %d, age: %lld sec)",
                                 eventID.c_str(), event.status, psm_->getEventAge());
                        ESP_LOGW(TAG, "Operation was likely interrupted by power loss - check if recovery needed");
                    }
                } else {
                    ESP_LOGD(TAG, "PSM event is empty (cleared) - no restoration needed");
                }
            } else {
                ESP_LOGI(TAG, "No PSM event found - clean boot");
            }
        }
    }

    if (elapsed >= INIT_DURATION) {
        ESP_LOGI(TAG, "Boot sequence complete");

        // Check if we need to restore a persisted state from PSM
        if (boot_restore_pending_) {
            boot_restore_pending_ = false;

            if (boot_restore_state_ == ControllerState::SHUTDOWN) {
                ESP_LOGW(TAG, "RECOVERY: Restoring SHUTDOWN state from power loss");
                transitionTo(ControllerState::SHUTDOWN);
            } else if (boot_restore_state_ == ControllerState::PAUSE) {
                ESP_LOGW(TAG, "RECOVERY: Restoring PAUSE state from power loss");
                transitionTo(ControllerState::PAUSE);
            } else {
                // Default to IDLE
                transitionTo(ControllerState::IDLE);
            }
        } else {
            // Normal boot - no persisted state to restore
            transitionTo(ControllerState::IDLE);
        }
    }
}

void PlantOSController::handleIdle() {
    // Breathing green - waiting for commands
    // This is the default ready state

    uint32_t elapsed = getStateElapsed();

    // Periodically check PSM for SHUTDOWN or PAUSE states every 5 seconds
    // This handles cases where:
    // 1. State was set while system was running
    // 2. State restoration from INIT failed due to timing issues
    // 3. Component initialization order caused PSM to be unavailable during INIT
    static uint32_t last_psm_check = 0;
    uint32_t now = hal_->getSystemTime();

    if (now - last_psm_check >= 5000) {  // Check every 5 seconds
        last_psm_check = now;

        if (psm_ && psm_->hasEvent()) {
            esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
            std::string eventID(event.eventID);

            ESP_LOGD(TAG, ">>> IDLE PSM CHECK: EventID='%s', Status=%d, Age=%lld sec",
                     eventID.c_str(), event.status, psm_->getEventAge());

            // Check for persistent states that should override IDLE
            if (!eventID.empty()) {
                if (eventID == "STATE_SHUTDOWN") {
                    ESP_LOGW(TAG, ">>> PSM check: SHUTDOWN state detected - transitioning from IDLE");
                    transitionTo(ControllerState::SHUTDOWN);
                    return;
                } else if (eventID == "STATE_PAUSE") {
                    ESP_LOGW(TAG, ">>> PSM check: PAUSE state detected - transitioning from IDLE");
                    transitionTo(ControllerState::PAUSE);
                    return;
                }
            }
        }
    }

    // Future: Check for scheduled tasks, sensor thresholds, etc.
}

void PlantOSController::handleShutdown() {
    // Solid yellow LED handled by LedBehaviorSystem
    // All actuators OFF, calendar/time-based events disabled
    // Persists across power cycles - must use setToIdle() to exit

    uint32_t elapsed = getStateElapsed();

    // On entry: Turn off all pumps and valves for safety
    if (elapsed < 100) {
        turnOffAllPumps();
        ESP_LOGI(TAG, "SHUTDOWN state ACTIVE - all actuators OFF, calendar disabled");

        // Persist SHUTDOWN state to NVS
        if (psm_) {
            ESP_LOGW(TAG, ">>> SAVING TO PSM: STATE_SHUTDOWN (status=1)");
            psm_->logEvent("STATE_SHUTDOWN", 1);  // 1 = ENTERED_SHUTDOWN

            // Verify it was saved
            if (psm_->hasEvent()) {
                auto event = psm_->getLastEvent();
                ESP_LOGW(TAG, ">>> PSM VERIFICATION: EventID='%s', Status=%d",
                         event.eventID, event.status);
            }
        } else {
            ESP_LOGE(TAG, ">>> PSM NOT AVAILABLE - SHUTDOWN STATE NOT PERSISTED!");
        }

        // Update status logger
        status_logger_.updateMaintenanceMode(true);

        return;
    }

    // Stay in this state until setToIdle() is called
    // No automatic exit - persists across reboots
}

void PlantOSController::handlePause() {
    // Solid orange LED handled by LedBehaviorSystem
    // Actuators maintain current state, calendar/time-based events disabled
    // Persists across power cycles - must use setToIdle() to exit

    uint32_t elapsed = getStateElapsed();

    // On entry: Log state change
    if (elapsed < 100) {
        ESP_LOGI(TAG, "PAUSE state ACTIVE - actuators maintained, calendar disabled");

        // Persist PAUSE state to NVS
        if (psm_) {
            ESP_LOGW(TAG, ">>> SAVING TO PSM: STATE_PAUSE (status=1)");
            psm_->logEvent("STATE_PAUSE", 1);  // 1 = ENTERED_PAUSE

            // Verify it was saved
            if (psm_->hasEvent()) {
                auto event = psm_->getLastEvent();
                ESP_LOGW(TAG, ">>> PSM VERIFICATION: EventID='%s', Status=%d",
                         event.eventID, event.status);
            }
        } else {
            ESP_LOGE(TAG, ">>> PSM NOT AVAILABLE - PAUSE STATE NOT PERSISTED!");
        }

        return;
    }

    // Stay in this state until setToIdle() is called
    // No automatic exit - persists across reboots
}

void PlantOSController::handleError() {
    // Fast red flash LED handled by LedBehaviorSystem
    // Error condition with automatic recovery to INIT after 5 seconds

    uint32_t elapsed = getStateElapsed();

    // On entry: Turn off all pumps for safety
    if (elapsed < 100) {
        turnOffAllPumps();
        ESP_LOGE(TAG, "ERROR state: All pumps OFF for safety");
        return;
    }

    // Wait 5 seconds to display error
    if (elapsed >= ERROR_DURATION) {
        // Clear alerts before transitioning back to INIT
        status_logger_.clearAlert("PH_CRITICAL");
        status_logger_.clearAlert("PH_MAX_ATTEMPTS");
        status_logger_.clearAlert("SENSOR_CRITICAL");

        ESP_LOGI(TAG, "Error timeout - clearing alerts and restarting to INIT");
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

        // Send temperature compensation before starting pH measurements
        sendTemperatureCompensation();

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

            // Check for critical pH (out of safe range 5.0-7.5)
            if (ph < 5.0f || ph > 7.5f) {
                ESP_LOGE(TAG, "CRITICAL pH DETECTED: %.2f (safe range: 5.0-7.5)", ph);

                // Log critical pH event to PSM for persistence
                if (psm_) {
                    // Only log once per measurement cycle to avoid spam
                    if (ph_readings_.size() == 1) {
                        psm_->logEvent("PH_CRITICAL", 2);  // 2 = ERROR status
                        ESP_LOGW(TAG, "Critical pH event logged to PSM for recovery tracking");
                    }
                }

                // Add alert to status logger
                std::string alert_msg = "pH outside safe range: " + std::to_string(ph) + " (safe: 5.0-7.5)";
                status_logger_.updateAlertStatus("PH_CRITICAL", alert_msg);
            } else {
                // pH back in safe range - clear critical event if it exists
                if (psm_ && ph_readings_.size() == 1) {
                    // Check if there's a PH_CRITICAL event and clear it
                    if (psm_->hasEvent()) {
                        esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
                        std::string eventID(event.eventID);
                        if (eventID == "PH_CRITICAL") {
                            psm_->clearEvent();
                            status_logger_.clearAlert("PH_CRITICAL");
                            ESP_LOGI(TAG, "pH returned to safe range - cleared PH_CRITICAL event");
                        }
                    }
                }
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

        // Clear any PH_CRITICAL event since measurement failed
        if (psm_ && psm_->hasEvent()) {
            esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
            std::string eventID(event.eventID);
            if (eventID == "PH_CRITICAL") {
                psm_->clearEvent();
            }
        }

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

        // Clear PSM event - pH correction complete
        if (psm_) {
            psm_->clearEvent();
        }

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
    // Yellow fast blink - pH sensor calibration with robust averaging
    // 3-point calibration sequence: Mid (7.00) → Low (4.00) → High (10.01)
    // Each point waits for stable readings before proceeding

    if (!ph_sensor_ || !hal_) {
        ESP_LOGE(TAG, "pH sensor or HAL not configured - cannot calibrate");
        transitionTo(ControllerState::ERROR);
        return;
    }

    // Check if sensor hardware is actually connected and responding
    if (!ph_sensor_->is_sensor_ready()) {
        ESP_LOGE(TAG, "pH sensor hardware not responding - calibration aborted");
        ESP_LOGE(TAG, "Check UART connection and power to sensor");
        transitionTo(ControllerState::ERROR);
        return;
    }

    uint32_t now = esphome::millis();
    uint32_t elapsed = now - calib_step_start_time_;

    switch (calib_step_) {
        // ====================================================================
        // MID-POINT CALIBRATION (pH 7.00)
        // ====================================================================
        case CalibrationStep::MID_PROMPT:
            if (elapsed < 100) {  // Log prompt once at start
                ESP_LOGI(TAG, "");
                ESP_LOGI(TAG, "===========================================================");
                ESP_LOGI(TAG, "  STEP 1/3: MID-POINT CALIBRATION (pH 7.00)");
                ESP_LOGI(TAG, "===========================================================");
                ESP_LOGI(TAG, "1. Insert pH probe into pH 7.00 buffer solution");
                ESP_LOGI(TAG, "2. Waiting %d seconds for stability check...", CALIB_PROMPT_DURATION / 1000);
                ESP_LOGI(TAG, "");
            }

            if (elapsed >= CALIB_PROMPT_DURATION) {
                ESP_LOGI(TAG, "Starting stability check for mid-point calibration...");

                // Send temperature compensation before calibration readings
                sendTemperatureCompensation();

                resetCalibrationBatch();
                calib_step_ = CalibrationStep::MID_STABILIZING;
                calib_step_start_time_ = now;
            }
            break;

        case CalibrationStep::MID_STABILIZING:
            // Take readings (20 per batch, 1/second, 5 batches with 30s wait between)
            if (calib_readings_in_batch_ < CALIB_READINGS_PER_BATCH) {
                // Time to take next reading?
                if (now - calib_last_reading_time_ >= CALIB_READING_INTERVAL) {
                    float ph_value;
                    if (hal_->takeSinglePhReading(ph_value)) {
                        calib_batch_sum_ += ph_value;
                        calib_readings_in_batch_++;
                        calib_last_reading_time_ = now;
                        ESP_LOGD(TAG, "MID: Batch %d, Reading %d/%d: pH %.2f",
                                calib_current_batch_ + 1, calib_readings_in_batch_,
                                CALIB_READINGS_PER_BATCH, ph_value);
                    } else {
                        ESP_LOGW(TAG, "Failed to take pH reading - retrying");
                    }
                }
            } else {
                // Batch complete - calculate average
                float batch_avg = calib_batch_sum_ / CALIB_READINGS_PER_BATCH;
                calib_batch_averages_[calib_current_batch_] = batch_avg;
                ESP_LOGI(TAG, "MID: Batch %d complete - Average: pH %.2f",
                        calib_current_batch_ + 1, batch_avg);

                calib_current_batch_++;

                if (calib_current_batch_ >= CALIB_TOTAL_BATCHES) {
                    // All batches complete - check stability
                    if (checkCalibrationStability()) {
                        ESP_LOGI(TAG, "MID: Readings are STABLE - proceeding to calibration");
                        calib_step_ = CalibrationStep::MID_CALIBRATE;
                        calib_step_start_time_ = now;
                    } else {
                        ESP_LOGW(TAG, "MID: Readings NOT stable - restarting stability check");
                        resetCalibrationBatch();
                    }
                } else {
                    // Wait 30 seconds before next batch
                    ESP_LOGI(TAG, "MID: Waiting %d seconds before next batch...", CALIB_BATCH_WAIT / 1000);
                    calib_batch_complete_time_ = now;
                    calib_readings_in_batch_ = 0;  // Reset for next batch
                    calib_batch_sum_ = 0.0f;
                }
            }

            // Handle wait between batches
            if (calib_readings_in_batch_ == 0 && calib_current_batch_ > 0 &&
                calib_current_batch_ < CALIB_TOTAL_BATCHES) {
                if (now - calib_batch_complete_time_ < CALIB_BATCH_WAIT) {
                    // Still waiting
                    return;
                }
            }
            break;

        case CalibrationStep::MID_CALIBRATE:
            ESP_LOGI(TAG, "Sending mid-point calibration command at pH 7.00...");
            if (!hal_->startPhCalibration(7.00f, 0)) {  // 0 = mid
                ESP_LOGE(TAG, "MID: Calibration command FAILED");
                transitionTo(ControllerState::ERROR);
                return;
            }
            calib_step_ = CalibrationStep::MID_COMPLETE;
            calib_step_start_time_ = now;
            break;

        case CalibrationStep::MID_COMPLETE:
            if (elapsed >= 1000) {  // Wait 1s for command to complete
                ESP_LOGI(TAG, "MID: Mid-point calibration complete!");
                calib_step_ = CalibrationStep::LOW_PROMPT;
                calib_step_start_time_ = now;
            }
            break;

        // ====================================================================
        // LOW-POINT CALIBRATION (pH 4.00)
        // ====================================================================
        case CalibrationStep::LOW_PROMPT:
            if (elapsed < 100) {
                ESP_LOGI(TAG, "");
                ESP_LOGI(TAG, "===========================================================");
                ESP_LOGI(TAG, "  STEP 2/3: LOW-POINT CALIBRATION (pH 4.00)");
                ESP_LOGI(TAG, "===========================================================");
                ESP_LOGI(TAG, "1. Remove probe and rinse with distilled water");
                ESP_LOGI(TAG, "2. Insert pH probe into pH 4.00 buffer solution");
                ESP_LOGI(TAG, "3. Waiting %d seconds for stability check...", CALIB_PROMPT_DURATION / 1000);
                ESP_LOGI(TAG, "");
            }

            if (elapsed >= CALIB_PROMPT_DURATION) {
                ESP_LOGI(TAG, "Starting stability check for low-point calibration...");

                // Send temperature compensation before calibration readings
                sendTemperatureCompensation();

                resetCalibrationBatch();
                calib_step_ = CalibrationStep::LOW_STABILIZING;
                calib_step_start_time_ = now;
            }
            break;

        case CalibrationStep::LOW_STABILIZING:
            // Same logic as MID_STABILIZING
            if (calib_readings_in_batch_ < CALIB_READINGS_PER_BATCH) {
                if (now - calib_last_reading_time_ >= CALIB_READING_INTERVAL) {
                    float ph_value;
                    if (hal_->takeSinglePhReading(ph_value)) {
                        calib_batch_sum_ += ph_value;
                        calib_readings_in_batch_++;
                        calib_last_reading_time_ = now;
                        ESP_LOGD(TAG, "LOW: Batch %d, Reading %d/%d: pH %.2f",
                                calib_current_batch_ + 1, calib_readings_in_batch_,
                                CALIB_READINGS_PER_BATCH, ph_value);
                    }
                }
            } else {
                float batch_avg = calib_batch_sum_ / CALIB_READINGS_PER_BATCH;
                calib_batch_averages_[calib_current_batch_] = batch_avg;
                ESP_LOGI(TAG, "LOW: Batch %d complete - Average: pH %.2f",
                        calib_current_batch_ + 1, batch_avg);

                calib_current_batch_++;

                if (calib_current_batch_ >= CALIB_TOTAL_BATCHES) {
                    if (checkCalibrationStability()) {
                        ESP_LOGI(TAG, "LOW: Readings are STABLE - proceeding to calibration");
                        calib_step_ = CalibrationStep::LOW_CALIBRATE;
                        calib_step_start_time_ = now;
                    } else {
                        ESP_LOGW(TAG, "LOW: Readings NOT stable - restarting stability check");
                        resetCalibrationBatch();
                    }
                } else {
                    ESP_LOGI(TAG, "LOW: Waiting %d seconds before next batch...", CALIB_BATCH_WAIT / 1000);
                    calib_batch_complete_time_ = now;
                    calib_readings_in_batch_ = 0;
                    calib_batch_sum_ = 0.0f;
                }
            }

            if (calib_readings_in_batch_ == 0 && calib_current_batch_ > 0 &&
                calib_current_batch_ < CALIB_TOTAL_BATCHES) {
                if (now - calib_batch_complete_time_ < CALIB_BATCH_WAIT) {
                    return;
                }
            }
            break;

        case CalibrationStep::LOW_CALIBRATE:
            ESP_LOGI(TAG, "Sending low-point calibration command at pH 4.00...");
            if (!hal_->startPhCalibration(4.00f, 1)) {  // 1 = low
                ESP_LOGE(TAG, "LOW: Calibration command FAILED");
                transitionTo(ControllerState::ERROR);
                return;
            }
            calib_step_ = CalibrationStep::LOW_COMPLETE;
            calib_step_start_time_ = now;
            break;

        case CalibrationStep::LOW_COMPLETE:
            if (elapsed >= 1000) {
                ESP_LOGI(TAG, "LOW: Low-point calibration complete!");
                calib_step_ = CalibrationStep::HIGH_PROMPT;
                calib_step_start_time_ = now;
            }
            break;

        // ====================================================================
        // HIGH-POINT CALIBRATION (pH 10.01)
        // ====================================================================
        case CalibrationStep::HIGH_PROMPT:
            if (elapsed < 100) {
                ESP_LOGI(TAG, "");
                ESP_LOGI(TAG, "===========================================================");
                ESP_LOGI(TAG, "  STEP 3/3: HIGH-POINT CALIBRATION (pH 10.01)");
                ESP_LOGI(TAG, "===========================================================");
                ESP_LOGI(TAG, "1. Remove probe and rinse with distilled water");
                ESP_LOGI(TAG, "2. Insert pH probe into pH 10.01 buffer solution");
                ESP_LOGI(TAG, "3. Waiting %d seconds for stability check...", CALIB_PROMPT_DURATION / 1000);
                ESP_LOGI(TAG, "");
            }

            if (elapsed >= CALIB_PROMPT_DURATION) {
                ESP_LOGI(TAG, "Starting stability check for high-point calibration...");

                // Send temperature compensation before calibration readings
                sendTemperatureCompensation();

                resetCalibrationBatch();
                calib_step_ = CalibrationStep::HIGH_STABILIZING;
                calib_step_start_time_ = now;
            }
            break;

        case CalibrationStep::HIGH_STABILIZING:
            // Same logic as MID_STABILIZING and LOW_STABILIZING
            if (calib_readings_in_batch_ < CALIB_READINGS_PER_BATCH) {
                if (now - calib_last_reading_time_ >= CALIB_READING_INTERVAL) {
                    float ph_value;
                    if (hal_->takeSinglePhReading(ph_value)) {
                        calib_batch_sum_ += ph_value;
                        calib_readings_in_batch_++;
                        calib_last_reading_time_ = now;
                        ESP_LOGD(TAG, "HIGH: Batch %d, Reading %d/%d: pH %.2f",
                                calib_current_batch_ + 1, calib_readings_in_batch_,
                                CALIB_READINGS_PER_BATCH, ph_value);
                    }
                }
            } else {
                float batch_avg = calib_batch_sum_ / CALIB_READINGS_PER_BATCH;
                calib_batch_averages_[calib_current_batch_] = batch_avg;
                ESP_LOGI(TAG, "HIGH: Batch %d complete - Average: pH %.2f",
                        calib_current_batch_ + 1, batch_avg);

                calib_current_batch_++;

                if (calib_current_batch_ >= CALIB_TOTAL_BATCHES) {
                    if (checkCalibrationStability()) {
                        ESP_LOGI(TAG, "HIGH: Readings are STABLE - proceeding to calibration");
                        calib_step_ = CalibrationStep::HIGH_CALIBRATE;
                        calib_step_start_time_ = now;
                    } else {
                        ESP_LOGW(TAG, "HIGH: Readings NOT stable - restarting stability check");
                        resetCalibrationBatch();
                    }
                } else {
                    ESP_LOGI(TAG, "HIGH: Waiting %d seconds before next batch...", CALIB_BATCH_WAIT / 1000);
                    calib_batch_complete_time_ = now;
                    calib_readings_in_batch_ = 0;
                    calib_batch_sum_ = 0.0f;
                }
            }

            if (calib_readings_in_batch_ == 0 && calib_current_batch_ > 0 &&
                calib_current_batch_ < CALIB_TOTAL_BATCHES) {
                if (now - calib_batch_complete_time_ < CALIB_BATCH_WAIT) {
                    return;
                }
            }
            break;

        case CalibrationStep::HIGH_CALIBRATE:
            ESP_LOGI(TAG, "Sending high-point calibration command at pH 10.01...");
            if (!hal_->startPhCalibration(10.01f, 2)) {  // 2 = high
                ESP_LOGE(TAG, "HIGH: Calibration command FAILED");
                transitionTo(ControllerState::ERROR);
                return;
            }
            calib_step_ = CalibrationStep::HIGH_COMPLETE;
            calib_step_start_time_ = now;
            break;

        case CalibrationStep::HIGH_COMPLETE:
            if (elapsed >= 1000) {
                ESP_LOGI(TAG, "HIGH: High-point calibration complete!");
                calib_step_ = CalibrationStep::COMPLETE;
                calib_step_start_time_ = now;
            }
            break;

        // ====================================================================
        // CALIBRATION COMPLETE
        // ====================================================================
        case CalibrationStep::COMPLETE:
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "===========================================================");
            ESP_LOGI(TAG, "  3-POINT pH CALIBRATION COMPLETE!");
            ESP_LOGI(TAG, "===========================================================");
            ESP_LOGI(TAG, "Remove probe from buffer solution");
            ESP_LOGI(TAG, "Rinse with distilled water and return to tank");
            ESP_LOGI(TAG, "System returning to IDLE state...");
            ESP_LOGI(TAG, "");

            // Query calibration status to verify
            ph_sensor_->query_calibration_status();

            // Clear PSM event
            if (psm_) {
                psm_->clearEvent();
            }

            // Reset calibration state
            calib_step_ = CalibrationStep::IDLE;

            // Return to IDLE
            transitionTo(ControllerState::IDLE);
            break;

        case CalibrationStep::IDLE:
        default:
            ESP_LOGW(TAG, "Unexpected calibration step");
            transitionTo(ControllerState::IDLE);
            break;
    }
}

void PlantOSController::handleFeeding() {
    // Orange pulse LED handled by LedBehaviorSystem
    // Sequential nutrient pump activation: A → B → C

    uint32_t elapsed = getStateElapsed();

    // On first entry to feeding state: send temperature compensation
    // This ensures pH sensor is properly compensated if pH is checked during/after feeding
    if (state_counter_ == 0 && elapsed < 100) {
        sendTemperatureCompensation();
    }

    // Phase 7 TODO: Get durations from CalendarManager
    // For now, use hardcoded example durations (in milliseconds)
    static constexpr uint32_t NUTRIENT_A_DURATION = 5000;  // 5 seconds
    static constexpr uint32_t NUTRIENT_B_DURATION = 4000;  // 4 seconds
    static constexpr uint32_t NUTRIENT_C_DURATION = 3000;  // 3 seconds

    // Use state_counter to track which pump we're on (0=A, 1=B, 2=C, 3=done)
    const char* pump_name = nullptr;
    uint32_t duration_ms = 0;

    if (state_counter_ == 0) {
        pump_name = NUTRIENT_PUMP_A;
        duration_ms = NUTRIENT_A_DURATION;
    } else if (state_counter_ == 1) {
        pump_name = NUTRIENT_PUMP_B;
        duration_ms = NUTRIENT_B_DURATION;
    } else if (state_counter_ == 2) {
        pump_name = NUTRIENT_PUMP_C;
        duration_ms = NUTRIENT_C_DURATION;
    } else {
        // All pumps complete
        ESP_LOGI(TAG, "Feeding sequence complete");

        // Clear PSM event - feeding complete
        if (psm_) {
            psm_->clearEvent();
        }

        transitionTo(ControllerState::IDLE);
        return;
    }

    // On entry for this pump: activate it
    if (elapsed < 100) {
        if (duration_ms > 0) {
            if (safety_gate_) {
                uint32_t duration_sec = (duration_ms + 999) / 1000;  // Round up to seconds
                bool approved = safety_gate_->executeCommand(pump_name, true, duration_sec);

                if (!approved) {
                    ESP_LOGE(TAG, "%s command rejected by SafetyGate!", pump_name);

                    // Clear PSM event - feeding aborted due to safety rejection
                    if (psm_) {
                        psm_->clearEvent();
                    }

                    // Abort feeding sequence
                    transitionTo(ControllerState::IDLE);
                    return;
                }
            }

            ESP_LOGI(TAG, "%s ON for %d ms", pump_name, duration_ms);
        } else {
            ESP_LOGI(TAG, "%s duration is 0 - skipping", pump_name);
            // Immediately move to next pump
            state_counter_++;
            state_start_time_ = hal_->getSystemTime();
        }
        return;
    }

    // Wait for duration + 200ms safety margin
    if (elapsed < duration_ms + 200) {
        return;
    }

    // Turn off current pump explicitly
    if (safety_gate_) {
        safety_gate_->executeCommand(pump_name, false, 0);
    }

    ESP_LOGI(TAG, "%s complete", pump_name);

    // Move to next pump
    state_counter_++;
    state_start_time_ = hal_->getSystemTime();
}

void PlantOSController::handleWaterFilling() {
    // Blue solid LED handled by LedBehaviorSystem
    // Open water valve for fixed duration

    uint32_t elapsed = getStateElapsed();

    // Fill duration: 30 seconds (matches old PlantOSLogic implementation)
    static constexpr uint32_t FILL_DURATION_MS = 30000;

    // On entry: open water valve
    if (elapsed < 100) {
        if (safety_gate_) {
            bool approved = safety_gate_->executeCommand(WATER_VALVE, true, 30);  // 30 seconds

            if (!approved) {
                ESP_LOGE(TAG, "Water valve command rejected by SafetyGate!");

                // Clear PSM event - water fill aborted due to safety rejection
                if (psm_) {
                    psm_->clearEvent();
                }

                transitionTo(ControllerState::IDLE);
                return;
            }
        }

        ESP_LOGI(TAG, "Water valve OPEN for %d seconds", FILL_DURATION_MS / 1000);
        return;
    }

    // Wait for fill duration + 200ms safety margin
    if (elapsed < FILL_DURATION_MS + 200) {
        return;
    }

    // Close valve explicitly
    if (safety_gate_) {
        safety_gate_->executeCommand(WATER_VALVE, false, 0);
    }

    ESP_LOGI(TAG, "Water fill complete");

    // Clear PSM event - water fill complete
    if (psm_) {
        psm_->clearEvent();
    }

    transitionTo(ControllerState::IDLE);
}

void PlantOSController::handleWaterEmptying() {
    // Purple pulse LED handled by LedBehaviorSystem
    // Activate wastewater pump for fixed duration

    uint32_t elapsed = getStateElapsed();

    // Empty duration: 30 seconds (matches old PlantOSLogic implementation)
    static constexpr uint32_t EMPTY_DURATION_MS = 30000;

    // On entry: activate wastewater pump
    if (elapsed < 100) {
        if (safety_gate_) {
            bool approved = safety_gate_->executeCommand(WASTEWATER_PUMP, true, 30);  // 30 seconds

            if (!approved) {
                ESP_LOGE(TAG, "Wastewater pump command rejected by SafetyGate!");

                // Clear PSM event - water empty aborted due to safety rejection
                if (psm_) {
                    psm_->clearEvent();
                }

                transitionTo(ControllerState::IDLE);
                return;
            }
        }

        ESP_LOGI(TAG, "Wastewater pump ON for %d seconds", EMPTY_DURATION_MS / 1000);
        return;
    }

    // Wait for empty duration + 200ms safety margin
    if (elapsed < EMPTY_DURATION_MS + 200) {
        return;
    }

    // Turn off pump explicitly
    if (safety_gate_) {
        safety_gate_->executeCommand(WASTEWATER_PUMP, false, 0);
    }

    ESP_LOGI(TAG, "Water empty complete");

    // Clear PSM event - water empty complete
    if (psm_) {
        psm_->clearEvent();
    }

    transitionTo(ControllerState::IDLE);
}

// ============================================================================
// pH Processing State Handler
// ============================================================================

void PlantOSController::handlePhProcessing() {
    // Yellow pulse - Processing periodic pH reading to decide if correction needed
    // Called every 2 hours (configurable in HAL) to check if pH is in range

    if (!hal_) {
        ESP_LOGE(TAG, "HAL not configured - cannot process pH");
        transitionTo(ControllerState::ERROR);
        return;
    }

    // Read current pH value
    float ph_value = hal_->readPH();

    if (!hal_->hasPhValue()) {
        ESP_LOGW(TAG, "No pH value available - returning to IDLE");
        transitionTo(ControllerState::IDLE);
        return;
    }

    // Get pH range from HAL configuration
    float ph_min = hal_->get_ph_min();
    float ph_max = hal_->get_ph_max();

    ESP_LOGI(TAG, "========================================================");
    ESP_LOGI(TAG, "  PERIODIC pH CHECK");
    ESP_LOGI(TAG, "========================================================");
    ESP_LOGI(TAG, "Current pH: %.2f", ph_value);
    ESP_LOGI(TAG, "Target range: %.2f - %.2f", ph_min, ph_max);

    // Check if pH is within range
    if (ph_value >= ph_min && ph_value <= ph_max) {
        ESP_LOGI(TAG, "pH is within range - no correction needed");
        ESP_LOGI(TAG, "Returning to IDLE");
        ESP_LOGI(TAG, "========================================================");
        transitionTo(ControllerState::IDLE);
    } else if (ph_value > ph_max) {
        ESP_LOGW(TAG, "pH is TOO HIGH (%.2f > %.2f)", ph_value, ph_max);
        ESP_LOGI(TAG, "Starting pH correction sequence");
        ESP_LOGI(TAG, "========================================================");

        // Reset pH correction state
        ph_attempt_count_ = 0;
        ph_readings_.clear();
        ph_current_ = 0.0f;
        ph_dose_duration_ms_ = 0;

        // Log event for crash recovery
        if (psm_) {
            psm_->logEvent("PH_CORRECTION", 0);  // 0 = STARTED
        }

        transitionTo(ControllerState::PH_MEASURING);
    } else {
        ESP_LOGW(TAG, "pH is TOO LOW (%.2f < %.2f)", ph_value, ph_min);
        ESP_LOGW(TAG, "WARNING: pH correction only supports LOWERING pH (adding acid)");
        ESP_LOGW(TAG, "Manual intervention required to raise pH");
        ESP_LOGI(TAG, "========================================================");
        transitionTo(ControllerState::IDLE);
    }
}

// ============================================================================
// Calibration Helper Methods
// ============================================================================

void PlantOSController::resetCalibrationBatch() {
    // Reset all calibration batch tracking variables
    ESP_LOGD(TAG, "Resetting calibration batch tracking");

    for (size_t i = 0; i < CALIB_TOTAL_BATCHES; i++) {
        calib_batch_averages_[i] = 0.0f;
    }

    calib_current_batch_ = 0;
    calib_readings_in_batch_ = 0;
    calib_batch_sum_ = 0.0f;
    calib_last_reading_time_ = 0;
    calib_batch_complete_time_ = 0;
}

bool PlantOSController::checkCalibrationStability() {
    // Check if last 3 batches are within 0.1 pH difference
    // Requires at least 3 batches to check

    if (calib_current_batch_ < 3) {
        ESP_LOGW(TAG, "Not enough batches to check stability (need 3, have %d)",
                 calib_current_batch_);
        return false;
    }

    // Get last 3 batch averages
    float batch1 = calib_batch_averages_[calib_current_batch_ - 3];
    float batch2 = calib_batch_averages_[calib_current_batch_ - 2];
    float batch3 = calib_batch_averages_[calib_current_batch_ - 1];

    // Calculate max difference between any two batches
    float diff_1_2 = std::abs(batch1 - batch2);
    float diff_2_3 = std::abs(batch2 - batch3);
    float diff_1_3 = std::abs(batch1 - batch3);

    float max_diff = std::max({diff_1_2, diff_2_3, diff_1_3});

    ESP_LOGI(TAG, "Stability check:");
    ESP_LOGI(TAG, "  Batch %d: pH %.2f", calib_current_batch_ - 2, batch1);
    ESP_LOGI(TAG, "  Batch %d: pH %.2f", calib_current_batch_ - 1, batch2);
    ESP_LOGI(TAG, "  Batch %d: pH %.2f", calib_current_batch_, batch3);
    ESP_LOGI(TAG, "  Max difference: %.3f pH (threshold: %.1f)",
             max_diff, CALIB_STABILITY_THRESHOLD);

    bool is_stable = max_diff <= CALIB_STABILITY_THRESHOLD;

    if (is_stable) {
        ESP_LOGI(TAG, "  Result: STABLE (difference within threshold)");
    } else {
        ESP_LOGW(TAG, "  Result: NOT STABLE (difference exceeds threshold)");
    }

    return is_stable;
}

void PlantOSController::sendTemperatureCompensation() {
    if (!hal_->hasTemperature()) {
        ESP_LOGW(TAG, "Temperature sensor not available - skipping compensation");
        return;
    }

    float temp = hal_->readTemperature();
    ESP_LOGI(TAG, "Sending temperature compensation: %.1f°C", temp);

    if (hal_->sendPhTemperatureCompensation(temp)) {
        ESP_LOGD(TAG, "Temperature compensation successful");
    } else {
        ESP_LOGW(TAG, "Temperature compensation failed");
    }
}

// ============================================================================
// Public API
// ============================================================================

void PlantOSController::startPhCorrection() {
    if (current_state_ != ControllerState::IDLE) {
        ESP_LOGW(TAG, "Cannot start pH correction - system not in IDLE state (state: %d)", static_cast<int>(current_state_));
        return;
    }

    ESP_LOGI(TAG, "Starting pH correction sequence");

    // Reset pH correction state
    ph_attempt_count_ = 0;
    ph_readings_.clear();
    ph_current_ = 0.0f;
    ph_dose_duration_ms_ = 0;

    // Log event for crash recovery persistence
    if (psm_) {
        psm_->logEvent("PH_CORRECTION", 0);  // 0 = STARTED
    }

    transitionTo(ControllerState::PH_MEASURING);
}

void PlantOSController::startPhCalibration() {
    if (current_state_ != ControllerState::IDLE) {
        ESP_LOGW(TAG, "Cannot start pH calibration - system not in IDLE state (state: %s)",
                 getStateAsString().c_str());
        return;
    }

    if (!ph_sensor_) {
        ESP_LOGE(TAG, "Cannot start pH calibration - pH sensor component not configured");
        return;
    }

    // Check if sensor hardware is actually connected and responding
    if (!ph_sensor_->is_sensor_ready()) {
        ESP_LOGE(TAG, "Cannot start pH calibration - sensor hardware not responding");
        ESP_LOGE(TAG, "Check UART connection (TX=GPIO4, RX=GPIO5) and power to sensor");
        ESP_LOGE(TAG, "Verify sensor shows up in Central Status Logger");
        return;
    }

    ESP_LOGI(TAG, "===============================================");
    ESP_LOGI(TAG, "  STARTING 3-POINT pH CALIBRATION SEQUENCE");
    ESP_LOGI(TAG, "===============================================");
    ESP_LOGI(TAG, "Calibration points: 4.00 (low), 7.00 (mid), 10.01 (high)");
    ESP_LOGI(TAG, "Follow the prompts to insert probe into each solution");

    // Reset calibration state
    calib_step_ = CalibrationStep::MID_PROMPT;
    calib_step_start_time_ = esphome::millis();

    // Log event for crash recovery persistence
    if (psm_) {
        psm_->logEvent("PH_CALIBRATION", 0);  // 0 = STARTED
    }

    transitionTo(ControllerState::PH_CALIBRATING);
}

void PlantOSController::startFeeding() {
    if (current_state_ == ControllerState::IDLE) {
        ESP_LOGI(TAG, "Starting feeding sequence");

        // Log event for crash recovery persistence
        if (psm_) {
            psm_->logEvent("FEEDING", 0);  // 0 = STARTED
        }

        transitionTo(ControllerState::FEEDING);
    } else {
        ESP_LOGW(TAG, "Cannot start feeding - system busy");
    }
}

void PlantOSController::startFillTank() {
    if (current_state_ == ControllerState::IDLE) {
        ESP_LOGI(TAG, "Starting tank fill");

        // Log event for crash recovery persistence
        if (psm_) {
            psm_->logEvent("WATER_FILL", 0);  // 0 = STARTED
        }

        transitionTo(ControllerState::WATER_FILLING);
    } else {
        ESP_LOGW(TAG, "Cannot start fill - system busy");
    }
}

void PlantOSController::startEmptyTank() {
    if (current_state_ == ControllerState::IDLE) {
        ESP_LOGI(TAG, "Starting tank drain");

        // Log event for crash recovery persistence
        if (psm_) {
            psm_->logEvent("WATER_EMPTY", 0);  // 0 = STARTED
        }

        transitionTo(ControllerState::WATER_EMPTYING);
    } else {
        ESP_LOGW(TAG, "Cannot start drain - system busy");
    }
}

void PlantOSController::setToShutdown() {
    ESP_LOGI(TAG, "Transitioning to SHUTDOWN state");
    transitionTo(ControllerState::SHUTDOWN);
}

void PlantOSController::setToPause() {
    ESP_LOGI(TAG, "Transitioning to PAUSE state");
    transitionTo(ControllerState::PAUSE);
}

void PlantOSController::setToIdle() {
    if (current_state_ == ControllerState::SHUTDOWN || current_state_ == ControllerState::PAUSE) {
        ESP_LOGI(TAG, "Exiting %s state to IDLE",
                 current_state_ == ControllerState::SHUTDOWN ? "SHUTDOWN" : "PAUSE");

        // Clear PSM persistence
        if (psm_) {
            psm_->clearEvent();
        }

        // Update status logger
        status_logger_.updateMaintenanceMode(false);

        transitionTo(ControllerState::IDLE);
    } else {
        ESP_LOGW(TAG, "setToIdle() called but not in SHUTDOWN or PAUSE state (current: %d)",
                 static_cast<int>(current_state_));
    }
}

void PlantOSController::resetToInit() {
    ESP_LOGI(TAG, "Manual reset requested");
    turnOffAllPumps();
    transitionTo(ControllerState::INIT);
}

void PlantOSController::configureStatusLogger(bool enableReports, uint32_t reportIntervalMs, bool verboseMode) {
    status_logger_.configure(enableReports, reportIntervalMs, verboseMode);
}

std::string PlantOSController::getStateAsString() const {
    // State name mapping for string representation
    const char* state_names[] = {
        "INIT", "IDLE", "SHUTDOWN", "PAUSE", "ERROR",
        "PH_PROCESSING", "PH_MEASURING", "PH_CALCULATING", "PH_INJECTING", "PH_MIXING", "PH_CALIBRATING",
        "FEEDING", "WATER_FILLING", "WATER_EMPTYING"
    };

    int state_index = static_cast<int>(current_state_);
    if (state_index >= 0 && state_index < 14) {
        return std::string(state_names[state_index]);
    }

    return "UNKNOWN";
}

// ============================================================================
// State Transition
// ============================================================================

void PlantOSController::transitionTo(ControllerState newState) {
    if (newState == current_state_) {
        return; // Already in this state
    }

    // State name mapping for readable logging
    const char* state_names[] = {
        "INIT", "IDLE", "SHUTDOWN", "PAUSE", "ERROR",
        "PH_PROCESSING", "PH_MEASURING", "PH_CALCULATING", "PH_INJECTING", "PH_MIXING", "PH_CALIBRATING",
        "FEEDING", "WATER_FILLING", "WATER_EMPTYING"
    };

    const char* oldStateName = state_names[static_cast<int>(current_state_)];
    const char* newStateName = state_names[static_cast<int>(newState)];

    ESP_LOGI(TAG, "State transition: %s → %s", oldStateName, newStateName);

    // Update status logger with new state
    status_logger_.updateControllerState(newStateName);

    // Verbose mode: Log detailed state transition info
    if (status_logger_.isVerboseMode()) {
        ESP_LOGI(TAG, "[VERBOSE] State change details:");
        ESP_LOGI(TAG, "[VERBOSE]   Previous state: %s", oldStateName);
        ESP_LOGI(TAG, "[VERBOSE]   New state: %s", newStateName);
        ESP_LOGI(TAG, "[VERBOSE]   Time in previous state: %u ms", getStateElapsed());
    }

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

    // Verbose mode: Log actuator commands
    if (status_logger_.isVerboseMode()) {
        ESP_LOGI(TAG, "[VERBOSE] Actuator command: %s → %s for %u seconds",
                 pumpId.c_str(), state ? "ON" : "OFF", durationSec);
    }

    return safety_gate_->executeCommand(pumpId.c_str(), state, durationSec);
}

bool PlantOSController::requestValve(const std::string& valveId, bool state, uint32_t durationSec) {
    if (!safety_gate_) {
        ESP_LOGW(TAG, "SafetyGate not available - cannot control %s", valveId.c_str());
        return false;
    }

    // Verbose mode: Log actuator commands
    if (status_logger_.isVerboseMode()) {
        ESP_LOGI(TAG, "[VERBOSE] Valve command: %s → %s for %u seconds",
                 valveId.c_str(), state ? "OPEN" : "CLOSED", durationSec);
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

    float ph = hal_->readPH();

    // Verbose mode: Log sensor readings (sample every 10th reading to avoid spam)
    static uint32_t reading_counter = 0;
    if (status_logger_.isVerboseMode() && (++reading_counter % 10 == 0)) {
        ESP_LOGI(TAG, "[VERBOSE] pH sensor reading: %.2f", ph);
    }

    return ph;
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
