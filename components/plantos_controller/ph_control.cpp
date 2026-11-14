// pH control: PH_* state handlers, pH calibration, acid dose and K-factor learning.
// Part of PlantOSController (declared in controller.h). FSMINFO.md is authoritative for FSM behavior.
#include "controller.h"
#include "dosing_math.h"
#include "esphome/components/plantos_hal/hal.h"
#include "esphome/components/actuator_safety_gate/ActuatorSafetyGate.h"
#include "esphome/components/persistent_state_manager/persistent_state_manager.h"
#include "esphome/components/calendar_manager/CalendarManager.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/alert_service/alert_service.h"
#include <algorithm>
#include <cmath>

namespace plantos_controller {

// ============================================================================
// pH correction state handlers
// ============================================================================

void PlantOSController::handlePhProcessing() {
    // Yellow pulse - Processing periodic pH reading to decide if correction needed
    // Called every 2 hours (configurable in HAL) to check if pH is in range

    if (!hal_) {
        ESP_LOGE(TAG, "HAL not configured - cannot process pH");

        // Set comprehensive alert before ERROR transition
        status_logger_.updateAlertWithContext(
            "HARDWARE_HAL_MISSING",
            "HAL not configured",
            "Hardware Abstraction Layer pointer is null - dependency injection failed",
            "Critical system error - check plantOS.yaml configuration and ESPHome logs",
            "System initialization incomplete",
            "System cannot operate without HAL. Restart required.",
            0
        );

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
        ph_dose_ml_ = 0.0f;
        ph_dose_duration_ms_ = 0;
        ph_cycle_start_ph_ = 0.0f;
        ph_cycle_total_ml_ = 0.0f;
        ph_cycle_water_filled_ = false;
        ph_cycle_aborted_ = false;

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

void PlantOSController::handlePhMeasuring() {
    // Yellow pulse - stabilizing pH reading (5 minutes)
    // All pumps OFF for accurate measurement

    uint32_t elapsed = getStateElapsed();

    // On entry: Turn off all pumps for stabilization (only once!)
    if (!state_entry_executed_) {
        state_entry_executed_ = true;
        turnOffAllPumps();

        // Select stabilization duration based on context (docs/CONTROL_LOGIC.md)
        uint32_t stabilize_ms = PH_MEASURING_DURATION;
        if (ph_post_fill_stabilize_) {
            stabilize_ms = POST_FILL_STABILIZE_MS;
            ESP_LOGI(TAG, "pH measuring: All pumps OFF for 10-min stabilization (post water fill)");
        } else if (ph_post_feed_stabilize_) {
            stabilize_ms = POST_FEED_STABILIZE_MS;
            ESP_LOGI(TAG, "pH measuring: All pumps OFF for 5-min stabilization (post EC feeding)");
        } else {
            ESP_LOGI(TAG, "pH measuring: All pumps OFF for %.0fs stabilization",
                     stabilize_ms / 1000.0f);
        }

        // Consume the stabilization intent flags: latch the chosen duration for
        // this measuring run and clear the flags immediately, so they can never
        // linger past a single PH_MEASURING entry (e.g. if this state is aborted).
        ph_measuring_duration_ms_ = stabilize_ms;
        ph_post_fill_stabilize_ = false;
        ph_post_feed_stabilize_ = false;

        ph_readings_.clear(); // Reset readings buffer

        // Send temperature compensation before starting pH measurements
        sendTemperatureCompensation();

        return;
    }

    // Take pH readings every 5 seconds
    uint32_t reading_interval = elapsed / 5000; // Every 5 seconds
    if (reading_interval > state_counter_) {
        state_counter_ = reading_interval;

        if (hasPhValue()) {
            // Sensor has value - check if recovering from failures
            if (sensor_retry_state_.consecutive_failures > 0) {
                ESP_LOGI(TAG, "pH sensor recovered after %u failures", sensor_retry_state_.consecutive_failures);
                sensor_retry_state_.reset();
                status_logger_.resolveAlert("PH_SENSOR_CRITICAL");
            }

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
            // Sensor has no value - implement retry logic with exponential backoff
            {
                sensor_retry_state_.recordFailure();

                if (!sensor_retry_state_.shouldRetry()) {
                    // Max retries exceeded - abort with comprehensive error
                    ESP_LOGE(TAG, "pH sensor not responding after %u attempts - aborting measurement",
                             sensor_retry_state_.consecutive_failures);

                    status_logger_.updateAlertWithContext(
                        "PH_SENSOR_CRITICAL",
                        "pH sensor not responding after " + std::to_string(sensor_retry_state_.consecutive_failures) + " attempts",
                        "Sensor has no value: hasPhValue() returned false for " +
                            std::to_string(sensor_retry_state_.consecutive_failures) + " consecutive readings",
                        "Check sensor wiring (UART TX=GPIO18, RX=GPIO19), verify sensor power (5V), try sensor calibration from web UI",
                        "pH measurement phase, " + std::to_string(ph_readings_.size()) +
                            " readings collected before failure",
                        "Aborting to IDLE. System will retry pH correction on next cycle.",
                        0
                    );

                    sensor_retry_state_.reset();

                    // Clear any PH_CRITICAL event since measurement failed
                    if (psm_ && psm_->hasEvent()) {
                        esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
                        std::string eventID(event.eventID);
                        if (eventID == "PH_CRITICAL") {
                            psm_->clearEvent();
                        }
                    }

                    transitionTo(ControllerState::IDLE);
                    return;
                }

                // Check if backoff period is complete
                uint32_t current_time = hal_ ? hal_->getSystemTime() : 0;
                if (!sensor_retry_state_.isBackoffComplete(current_time)) {
                    // Still in backoff - wait
                    uint32_t wait_time = (current_time - sensor_retry_state_.last_failure_time) / 1000;
                    uint32_t backoff_sec = sensor_retry_state_.backoff_delay_ms / 1000;
                    ESP_LOGD(TAG, "Sensor backoff: waiting %u/%u seconds",
                             wait_time, backoff_sec);
                    return;
                }

                // Backoff complete - ready for retry
                ESP_LOGW(TAG, "pH sensor retry attempt %u/%u (backoff: %u ms)",
                         sensor_retry_state_.consecutive_failures,
                         sensor_retry_state_.MAX_SENSOR_RETRIES,
                         sensor_retry_state_.backoff_delay_ms);
            }
        }
    }

    // Wait for stabilization period (duration latched at entry - see entry block above)
    if (elapsed < ph_measuring_duration_ms_) {
        return;
    }

    // Measurement period complete - calculate robust average
    if (ph_readings_.empty()) {
        ESP_LOGE(TAG, "No pH readings collected - aborting correction");

        // Set comprehensive alert before ERROR transition
        status_logger_.updateAlertWithContext(
            "NO_PH_READINGS",
            "No pH readings collected during 5-minute measurement phase",
            "Sensor returned no values: hasPhValue() was false for entire measurement period",
            "Check pH sensor UART connection (TX=GPIO18, RX=GPIO19), verify sensor power (5V), try sensor calibration from web UI",
            "pH correction aborted during measurement phase after 5 minutes",
            "System will return to IDLE and can retry on next cycle",
            0
        );

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

    // Get pH target range from calendar schedule for current grow day
    float target_min = PH_TARGET_MIN; // Fallback: 5.5
    float target_max = PH_TARGET_MAX; // Fallback: 6.5

    if (calendar_manager_) {
        // Use calendar manager's actual current day (not timestamp-based calculation)
        uint8_t current_day = calendar_manager_->get_current_day();
        auto schedule = calendar_manager_->get_schedule(current_day);
        target_min = schedule.target_ph_min;
        target_max = schedule.target_ph_max;
        ESP_LOGI(TAG, "pH: %.2f, Target (day %d): %.2f-%.2f (dose to midpoint: %.2f)",
                 ph_current_, current_day, target_min, target_max, (target_min + target_max) / 2.0f);
    } else {
        ESP_LOGI(TAG, "pH: %.2f, Target (default): %.2f-%.2f (dose to midpoint: %.2f)",
                 ph_current_, target_min, target_max, (target_min + target_max) / 2.0f);
    }

    // Check if pH is within target range
    if (ph_current_ >= target_min && ph_current_ <= target_max) {
        ESP_LOGI(TAG, "pH within target range - no correction needed");

        // Update K-factor if acid was actually dosed in this cycle
        if (ph_attempt_count_ > 0 && ph_cycle_total_ml_ > 0.1f) {
            updatePhKFactor(ph_cycle_start_ph_, ph_current_, ph_cycle_total_ml_);
        }

        // Record successful completion of pH correction operation
        recordOperationStep("pH_CORRECTION_SUCCESS");

        // Resolve any active pH-related alerts
        status_logger_.resolveAlert("PH_TOO_LOW");
        {
            status_logger_.resolveAlert("PH_CRITICAL");
            status_logger_.resolveAlert("NO_PH_READINGS");
            status_logger_.resolveAlert("PUMP_REJECTION_ACID");
            status_logger_.resolveAlert("PH_SENSOR_CRITICAL");
        }

        // Clear PSM event - pH correction complete
        if (psm_) {
            psm_->clearEvent();
        }

        transitionTo(ControllerState::IDLE);
        return;
    }

    // Check if pH is too low (cannot correct without a base pump)
    if (ph_current_ < target_min) {
        ESP_LOGW(TAG, "pH too low (%.2f < %.2f) - no base pump available, raising alert", ph_current_, target_min);
        char reason_buf[80];
        snprintf(reason_buf, sizeof(reason_buf), "pH %.2f below minimum %.2f", ph_current_, target_min);
        status_logger_.updateAlertWithContext(
            "PH_TOO_LOW",
            reason_buf,
            "pH too acidic — manual base addition required",
            "Detected in PH_CALCULATING after measurement cycle",
            "No base pump configured in system",
            "Add base solution manually or check acid pump for leaks"
        );
        transitionTo(ControllerState::IDLE);
        return;
    }

    // pH is too high - calculate required acid dose to reach midpoint of target range
    float ph_dose_target = (target_min + target_max) / 2.0f;
    float dose_ml = calculateAcidDoseML(ph_current_, ph_dose_target);

    // Check minimum dose threshold (avoid tiny corrections)
    if (dose_ml < 0.5f) {
        ESP_LOGI(TAG, "Calculated dose too small (%.1f mL < 0.5 mL) - skipping correction", dose_ml);
        transitionTo(ControllerState::IDLE);
        return;
    }

    // Convert mL to seconds using HAL pumpflow
    float dose_seconds = hal_->pumpflow("AcidPump", dose_ml);
    uint32_t dose_ms = static_cast<uint32_t>(dose_seconds * 1000.0f);

    // Check max attempts to prevent infinite loops
    if (ph_attempt_count_ >= MAX_PH_ATTEMPTS) {
        ESP_LOGE(TAG, "Max pH correction attempts reached (%d) - aborting", MAX_PH_ATTEMPTS);
        // Phase 7: Update alert status here
        transitionTo(ControllerState::IDLE);
        return;
    }

    // Proceed with injection
    ph_dose_ml_ = dose_ml;
    ph_dose_duration_ms_ = dose_ms;
    ph_attempt_count_++;

    // Track cycle start pH for K-factor learning (only on first injection)
    if (ph_attempt_count_ == 1) {
        ph_cycle_start_ph_ = ph_current_;
        ph_cycle_total_ml_ = 0.0f;
        ph_cycle_water_filled_ = false;
        ph_cycle_aborted_ = false;
        ESP_LOGI(TAG, "pH correction cycle started: start_pH=%.2f", ph_cycle_start_ph_);
    }

    ESP_LOGI(TAG, "pH correction needed: %.2f → %.2f (dose: %.1f mL = %.2f sec, attempt %d/%d)",
             ph_current_, target_max, dose_ml, dose_seconds, ph_attempt_count_, MAX_PH_ATTEMPTS);

    transitionTo(ControllerState::PH_INJECTING);
}

void PlantOSController::handlePhInjecting() {
    // Cyan pulse - acid dosing in progress
    // Air pump + Acid pump active (or acid pump only if no air pump)

    uint32_t elapsed = getStateElapsed();

    // On entry: Activate pumps (only once!)
    if (!state_entry_executed_) {
        state_entry_executed_ = true;

        // Calculate injection duration in seconds (round up)
        uint32_t injection_sec = (ph_dose_duration_ms_ + 999) / 1000;

        // Calculate mixing duration (same as handlePhMixing does)
        uint32_t mixing_duration_ms = calculatePhMixingDuration();
        uint32_t mixing_sec = (mixing_duration_ms + 999) / 1000;

        ESP_LOGI(TAG, "Starting acid injection: %.1f mL (%.2f sec)", ph_dose_ml_, injection_sec / 1.0f);

        // OPTIONAL: air pump cycling during pH injection — disabled based on test results.
        // Re-enable if poor acid mixing or pH measurement drift is observed.
        // if (safety_gate_) {
        //     uint32_t total_air_pump_sec = injection_sec + mixing_sec;
        //     std::vector<uint32_t> pattern = {total_air_pump_sec};
        //     if (!safety_gate_->setAirPumpPattern(pattern, true)) {
        //         ESP_LOGW(TAG, "AirPump pattern rejected - falling back to simple ON");
        //         requestPump(AIR_PUMP, true, 0);
        //     } else {
        //         ESP_LOGI(TAG, "AirPump pattern sent: %u sec", total_air_pump_sec);
        //     }
        // }

        // Request acid pump via SafetyGate with adaptive duration (will auto-adapt if needed)
        if (!requestPumpAdaptive(ACID_PUMP, true, injection_sec, false)) {
            ESP_LOGE(TAG, "Acid pump rejected by SafetyGate even after adaptation - aborting");
            // OPTIONAL: stop air pump sequence on abort — disabled (air pump not used during pH injection).
            // if (safety_gate_) { safety_gate_->stopAirPumpSequence(true); }

            // Set comprehensive alert before ERROR transition
            status_logger_.updateAlertWithContext(
                "PUMP_REJECTION_ACID",
                "Acid pump rejected by SafetyGate",
                "SafetyGate rejected acid pump command even after duration adaptation",
                "Check acid pump wiring, verify pump not stuck, check SafetyGate max duration config",
                "pH correction attempt " + std::to_string(ph_attempt_count_ + 1) + "/" +
                    std::to_string(MAX_PH_ATTEMPTS) + ", injection phase",
                "Aborting pH correction. Will retry on next cycle.",
                0
            );

            transitionTo(ControllerState::ERROR);
            return;
        }

        ESP_LOGI(TAG, "Acid pump active - dosing %.1f mL", ph_dose_ml_);

        // CRITICAL FIX: Reset timer to start from NOW (after HTTP delays)
        // The AirPump HTTP request is async, but we reset timer for consistency.
        state_start_time_ = hal_->getSystemTime();
        ESP_LOGD(TAG, "Timer reset: injection duration starts now");

        return;
    }

    // Wait for injection duration + 200ms safety margin
    uint32_t total_duration = ph_dose_duration_ms_ + 200;

    if (elapsed >= total_duration) {
        // Injection complete - explicitly stop acid pump
        requestPump(ACID_PUMP, false);

        // Accumulate total mL dosed in this correction cycle
        ph_cycle_total_ml_ += ph_dose_ml_;
        ESP_LOGI(TAG, "Acid dosing complete: %.1f mL added (cycle total: %.2f mL) - starting mixing phase",
                 ph_dose_ml_, ph_cycle_total_ml_);

        transitionTo(ControllerState::PH_MIXING);
    }
}

void PlantOSController::handlePhMixing() {
    // Blue pulse - mixing after acid injection
    // Air pump runs to distribute acid throughout tank (duration varies by tank volume)
    // NOTE: AirPump is controlled via Shelly pattern API - pattern was sent in PH_INJECTING
    uint32_t elapsed = getStateElapsed();

    // On entry: Calculate mixing duration for timer only (air pump already running via pattern)
    if (!state_entry_executed_) {
        state_entry_executed_ = true;

        // Calculate mixing duration based on tank volume (linear: 1.7L→30s, 70L→600s)
        ph_mixing_duration_ms_ = calculatePhMixingDuration();

        ESP_LOGI(TAG, "Mixing phase: waiting %.1f seconds (air pump controlled by Shelly pattern)",
                 ph_mixing_duration_ms_ / 1000.0f);

        // No air pump commands here - Shelly pattern handles the full injection+mixing duration
    }

    if (elapsed >= ph_mixing_duration_ms_) {
        ESP_LOGI(TAG, "pH mixing complete (%.1f seconds)", ph_mixing_duration_ms_ / 1000.0f);

        // OPTIONAL: restore air pump cycling after pH mixing — disabled (air pump not used during pH correction).
        // Re-enable if mixing quality needs improvement:
        // if (safety_gate_ && safety_gate_->isCyclingEnabled(AIR_PUMP)) {
        //     ESP_LOGI(TAG, "Restoring AirPump cycling pattern");
        //     safety_gate_->enableCycling(AIR_PUMP, true);
        // }

        // Loop back to PH_MEASURING to verify correction
        // Will continue until pH is in range OR max attempts reached
        ESP_LOGI(TAG, "Re-measuring pH to verify correction (attempt %d/%d)",
                 ph_attempt_count_, MAX_PH_ATTEMPTS);

        transitionTo(ControllerState::PH_MEASURING);
    }
}

// ============================================================================
// pH calibration
// ============================================================================

void PlantOSController::handlePhCalibrating() {
    // Yellow fast blink - pH sensor calibration with robust averaging
    // 3-point calibration sequence: Mid (7.00) → Low (4.00) → High (10.01)
    // Each point waits for stable readings before proceeding

    if (!hal_) {
        ESP_LOGE(TAG, "HAL not configured - cannot calibrate");
        transitionTo(ControllerState::ERROR);
        return;
    }

    // Check if sensor hardware is actually connected and responding
    if (!hal_->isPhSensorReady()) {
        ESP_LOGE(TAG, "pH sensor hardware not responding - calibration aborted");
        ESP_LOGE(TAG, "Check UART connection (TX=GPIO18, RX=GPIO19) and power to sensor");

        // Set comprehensive alert before ERROR transition
        status_logger_.updateAlertWithContext(
            "PH_SENSOR_HARDWARE_FAILURE",
            "pH sensor hardware not responding",
            "Sensor readiness check failed: isPhSensorReady() returned false",
            "Check UART connection (TX=GPIO18, RX=GPIO19), verify sensor power (5V), inspect sensor LED status",
            "Calibration aborted during initialization",
            "Fix hardware connection and retry calibration from web UI",
            0
        );

        // Ensure verbose mode is off even on error
        hal_->setPhVerbose(false);
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
                // Log when starting a new batch
                if (calib_readings_in_batch_ == 0 && calib_current_batch_ < CALIB_TOTAL_BATCHES) {
                    // Only log once when starting the batch (avoid spamming)
                    static int last_logged_batch = -1;
                    if (last_logged_batch != calib_current_batch_) {
                        ESP_LOGI(TAG, "MID: Starting batch %d/%d - Taking %d readings at 1-second intervals",
                                calib_current_batch_ + 1, CALIB_TOTAL_BATCHES, CALIB_READINGS_PER_BATCH);
                        last_logged_batch = calib_current_batch_;
                    }
                }

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
                ESP_LOGI(TAG, "MID: Batch %d/%d complete - Average: pH %.2f (Total readings: %d)",
                        calib_current_batch_ + 1, CALIB_TOTAL_BATCHES, batch_avg,
                        (calib_current_batch_ + 1) * CALIB_READINGS_PER_BATCH);

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

                // Set comprehensive alert before ERROR transition
                status_logger_.updateAlertWithContext(
                    "CALIBRATION_FAILED_MID",
                    "Mid-point calibration command failed (pH 7.00)",
                    "HAL calibration command returned false for mid-point (pH 7.00)",
                    "Verify pH sensor in pH 7.00 buffer solution, check sensor stability, retry calibration",
                    "3-point calibration: Step 1 of 3 (mid-point) failed",
                    "Calibration aborted. Verify buffer solution and sensor condition, then retry.",
                    0
                );

                // Ensure verbose mode is off even on error
                hal_->setPhVerbose(false);
                transitionTo(ControllerState::ERROR);
                return;
            } else {
                ESP_LOGI(TAG, "✓ MID: Calibration command sent successfully");
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
                // Log when starting a new batch
                if (calib_readings_in_batch_ == 0 && calib_current_batch_ < CALIB_TOTAL_BATCHES) {
                    // Only log once when starting the batch (avoid spamming)
                    static int last_logged_batch = -1;
                    if (last_logged_batch != calib_current_batch_) {
                        ESP_LOGI(TAG, "LOW: Starting batch %d/%d - Taking %d readings at 1-second intervals",
                                calib_current_batch_ + 1, CALIB_TOTAL_BATCHES, CALIB_READINGS_PER_BATCH);
                        last_logged_batch = calib_current_batch_;
                    }
                }

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
                ESP_LOGI(TAG, "LOW: Batch %d/%d complete - Average: pH %.2f (Total readings: %d)",
                        calib_current_batch_ + 1, CALIB_TOTAL_BATCHES, batch_avg,
                        (calib_current_batch_ + 1) * CALIB_READINGS_PER_BATCH);

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

                // Set comprehensive alert before ERROR transition
                status_logger_.updateAlertWithContext(
                    "CALIBRATION_FAILED_LOW",
                    "Low-point calibration command failed (pH 4.00)",
                    "HAL calibration command returned false for low-point (pH 4.00)",
                    "Verify pH sensor in pH 4.00 buffer solution, check sensor stability, retry calibration",
                    "3-point calibration: Step 2 of 3 (low-point) failed",
                    "Calibration aborted. Verify buffer solution and sensor condition, then retry.",
                    0
                );

                // Ensure verbose mode is off even on error
                hal_->setPhVerbose(false);
                transitionTo(ControllerState::ERROR);
                return;
            } else {
                ESP_LOGI(TAG, "✓ LOW: Calibration command sent successfully");
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
                // Log when starting a new batch
                if (calib_readings_in_batch_ == 0 && calib_current_batch_ < CALIB_TOTAL_BATCHES) {
                    // Only log once when starting the batch (avoid spamming)
                    static int last_logged_batch = -1;
                    if (last_logged_batch != calib_current_batch_) {
                        ESP_LOGI(TAG, "HIGH: Starting batch %d/%d - Taking %d readings at 1-second intervals",
                                calib_current_batch_ + 1, CALIB_TOTAL_BATCHES, CALIB_READINGS_PER_BATCH);
                        last_logged_batch = calib_current_batch_;
                    }
                }

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
                ESP_LOGI(TAG, "HIGH: Batch %d/%d complete - Average: pH %.2f (Total readings: %d)",
                        calib_current_batch_ + 1, CALIB_TOTAL_BATCHES, batch_avg,
                        (calib_current_batch_ + 1) * CALIB_READINGS_PER_BATCH);

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

                // Set comprehensive alert before ERROR transition
                status_logger_.updateAlertWithContext(
                    "CALIBRATION_FAILED_HIGH",
                    "High-point calibration command failed (pH 10.01)",
                    "HAL calibration command returned false for high-point (pH 10.01)",
                    "Verify pH sensor in pH 10.01 buffer solution, check sensor stability, retry calibration",
                    "3-point calibration: Step 3 of 3 (high-point) failed",
                    "Calibration aborted. Verify buffer solution and sensor condition, then retry.",
                    0
                );

                // Ensure verbose mode is off even on error
                hal_->setPhVerbose(false);
                transitionTo(ControllerState::ERROR);
                return;
            } else {
                ESP_LOGI(TAG, "✓ HIGH: Calibration command sent successfully");
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

            // Disable verbose mode - calibration complete
            hal_->setPhVerbose(false);
            ESP_LOGI(TAG, "Verbose mode DISABLED - returning to normal operation");

            // Query calibration status to verify
            hal_->queryPhCalibrationStatus();

            // Clear any calibration failure alerts from previous attempts
            status_logger_.resolveAlert("CALIBRATION_FAILED_MID");
            status_logger_.resolveAlert("CALIBRATION_FAILED_LOW");
            status_logger_.resolveAlert("CALIBRATION_FAILED_HIGH");

            // Save pH calibration timestamp for reminder tracking (Phase 7)
            if (psm_ && hal_ && hal_->hasTime()) {
                last_ph_calibration_ts_ = hal_->getCurrentTimestamp();
                psm_->saveInt32(NVS_KEY_PH_CALIB_TS, (int32_t)last_ph_calibration_ts_);
                ESP_LOGI(TAG, "pH calibration timestamp saved: %lld", (long long)last_ph_calibration_ts_);
                status_logger_.clearAlert("PH_CALIBRATION_DUE");
            }

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
    float max_diff = dosing_math::maxPairwiseDiff(batch1, batch2, batch3);

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
// pH dosing helpers
// ============================================================================

uint32_t PlantOSController::calculatePhMixingDuration() const {
    // Linear formula based on tank volume (docs/CONTROL_LOGIC.md, 70L-optimized):
    // - 1.7L → 30 seconds  (30,000 ms)
    // - 70L  → 600 seconds (600,000 ms = 10 min)
    // The formula itself is dosing_math::phMixingDurationRawMs().

    float volume_L = 10.0f;  // Default fallback
    if (hal_) {
        volume_L = hal_->getTankVolume();
    }

    // Apply linear formula
    float duration_ms = dosing_math::phMixingDurationRawMs(volume_L);

    // Safety bounds: min 30s, max 600s (10 min)
    if (duration_ms < 30000.0f) {
        ESP_LOGW(TAG, "Calculated mixing duration too short (%.1f s), using minimum 30s", duration_ms / 1000.0f);
        duration_ms = 30000.0f;
    } else if (duration_ms > 600000.0f) {
        ESP_LOGW(TAG, "Calculated mixing duration too long (%.1f s), using maximum 600s", duration_ms / 1000.0f);
        duration_ms = 600000.0f;
    }

    uint32_t duration_ms_int = static_cast<uint32_t>(duration_ms);
    ESP_LOGI(TAG, "pH mixing duration: %.1f seconds (tank: %.1f L)", duration_ms / 1000.0f, volume_L);

    return duration_ms_int;
}

float PlantOSController::calculateAcidDoseML(float current_ph, float target_ph_max) {
    // Adaptive K-factor formula (docs/CONTROL_LOGIC.md):
    // dose_ml = (current_ph - PH_CORRECTION_TARGET) * tank_volume_L * ph_K_
    //
    // PH_CORRECTION_TARGET (5.85) is the biological target, not the trigger threshold.
    // target_ph_max is the trigger threshold (from calendar), kept as parameter for callers.

    float ph_delta = current_ph - PH_CORRECTION_TARGET;
    if (ph_delta <= 0.0f) {
        return 0.0f;  // Already at or below target
    }

    float tank_volume_L = hal_ ? hal_->getTankVolume() : 10.0f;
    // dose = pH_delta × volume_L × K, clamped to [PH_MIN_DOSE_ML, PH_MAX_DOSE_ML]
    float dose_ml = dosing_math::acidDoseMl(ph_delta, tank_volume_L, ph_K_, PH_MIN_DOSE_ML, PH_MAX_DOSE_ML);

    ESP_LOGI(TAG, "Acid dose: %.3f pH_delta × %.1fL × K=%.4f = %.2f mL (clamped to [%.1f, %.1f])",
             ph_delta, tank_volume_L, ph_K_, dose_ml, PH_MIN_DOSE_ML, PH_MAX_DOSE_ML);

    return dose_ml;
}

void PlantOSController::updatePhKFactor(float ph_before, float ph_after, float ml_total) {
    float ph_delta = ph_before - ph_after;
    float tank_volume_L = hal_ ? hal_->getTankVolume() : 10.0f;

    // Guard conditions: only learn from clean, meaningful correction cycles
    if (ph_delta <= 0.05f) {
        ESP_LOGD(TAG, "K-factor update skipped: pH delta too small (%.3f < 0.05)", ph_delta);
        return;
    }
    if (ml_total <= 0.1f) {
        ESP_LOGD(TAG, "K-factor update skipped: ml dosed too small (%.2f < 0.1)", ml_total);
        return;
    }
    if (ph_cycle_water_filled_) {
        ESP_LOGD(TAG, "K-factor update skipped: water was added during cycle (dilution effect)");
        return;
    }
    if (ph_cycle_aborted_) {
        ESP_LOGD(TAG, "K-factor update skipped: cycle was aborted");
        return;
    }

    // K_observed = mL / (pH_delta × volume_L), clamped; K_new = EMA of K_observed and ph_K_
    const dosing_math::KUpdate k = dosing_math::phKUpdate(ml_total, ph_delta, tank_volume_L, ph_K_,
                                                          PH_K_EMA_ALPHA, PH_K_MIN_CLAMP, PH_K_MAX_CLAMP);
    float K_observed = k.observed;
    float K_new = k.updated;

    ESP_LOGI(TAG, "pH K-factor update: K_obs=%.4f → K_ema=%.4f (was %.4f) | "
             "pH %.2f→%.2f (delta=%.3f), %.2f mL, %.1f L",
             K_observed, K_new, ph_K_, ph_before, ph_after, ph_delta, ml_total, tank_volume_L);

    ph_K_ = K_new;
    if (ph_k_update_callback_) ph_k_update_callback_(ph_K_);

    // Persist to NVS
    if (psm_) {
        psm_->saveFloat(NVS_KEY_PH_K, ph_K_);
        ESP_LOGD(TAG, "pH K-factor saved to NVS: %.4f", ph_K_);
    }
}

float PlantOSController::calculateRobustPhAverage() {
    // Trimmed mean of ph_readings_ (10 % dropped from each end)
    return dosing_math::robustAverage(ph_readings_);
}

bool PlantOSController::isPhStable() {
    // Need at least 3 readings; the last 3 must be within 0.1 pH
    constexpr float STABILITY_THRESHOLD = 0.1f;
    return dosing_math::lastThreeWithin(ph_readings_, STABILITY_THRESHOLD);
}
} // namespace plantos_controller
