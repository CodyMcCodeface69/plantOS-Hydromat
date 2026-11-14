// EC control: FEEDING (calendar and mother mix doses), EC_* state handlers, EC calibration, EC K-factor learning.
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
// Nutrient dosing (calendar feed, manual installments, mother mix)
// ============================================================================

void PlantOSController::handleFeeding() {
    // EC-dependent feeding: splits the day's total calendar dose into installments of 1/5,
    // doses A → B → C per installment, then mixes and measures EC.
    // Repeats until EC target is reached (or FEEDING_MANUAL_MAX_CYCLES safety cap).
    // If no EC target is configured (ec_target == 0), falls back to single-pass fixed dose.
    //
    // This state is a setup stage only — it calculates doses and immediately hands off
    // to the EC_FEEDING → EC_MIXING → EC_MEASURING loop.

    if (!hal_ || !calendar_manager_) {
        ESP_LOGW(TAG, "[FEEDING] Missing HAL or CalendarManager - aborting");
        if (psm_) psm_->clearEvent();
        transitionTo(ControllerState::IDLE);
        return;
    }

    sendTemperatureCompensation();

    uint8_t current_day = calendar_manager_->get_current_day();
    auto schedule = calendar_manager_->get_schedule(current_day);

    float tank_volume_liters = is_reservoir_change_
        ? hal_->getTotalTankVolume()
        : hal_->getTankVolumeDelta();

    if (tank_volume_liters <= 0.0f) {
        ESP_LOGW(TAG, "[FEEDING] Tank volume unavailable - using 10 L");
        tank_volume_liters = 10.0f;
    }

    // Full calendar dose (EC target multiplier is applied to the EC target, not the dose itself)
    const dosing_math::NutrientDoses full = dosing_math::calendarDoses(
        schedule.nutrient_A_ml_per_liter, schedule.nutrient_B_ml_per_liter,
        schedule.nutrient_C_ml_per_liter, tank_volume_liters);

    if (schedule.ec_target > 0.0f) {
        // EC-dependent mode: divide total dose into installments of 1/5 and iterate
        const dosing_math::NutrientDoses installment = dosing_math::installmentDoses(full);
        ec_dose_A_ml_ = installment.a_ml;
        ec_dose_B_ml_ = installment.b_ml;
        ec_dose_C_ml_ = installment.c_ml;

        ec_pre_feeding_ = hal_->hasECValue() ? hal_->readEC() : 0.0f;
        ec_total_ml_per_L_ = 0.0f;
        ec_attempt_count_ = 0;
        feeding_manual_mode_ = true;

        ESP_LOGI(TAG, "[FEEDING] Day %d, %s %.1f L, EC %.1f -> target %.0f uS/cm: installments of 1/5 dose A %.2f B %.2f C %.2f mL",
                 current_day, is_reservoir_change_ ? "total" : "delta", tank_volume_liters, ec_pre_feeding_,
                 schedule.ec_target, ec_dose_A_ml_, ec_dose_B_ml_, ec_dose_C_ml_);

        if (psm_) psm_->logEvent("EC_FEEDING", 0);
        transitionTo(ControllerState::EC_FEEDING);
    } else {
        // Fixed-dose fallback: single pass A → B → C when EC target not configured
        ec_dose_A_ml_ = full.a_ml;
        ec_dose_B_ml_ = full.b_ml;
        ec_dose_C_ml_ = full.c_ml;
        feeding_manual_mode_ = false;

        ESP_LOGI(TAG, "[FEEDING] Day %d, %s %.1f L, no EC target: fixed dose A %.2f B %.2f C %.2f mL",
                 current_day, is_reservoir_change_ ? "total" : "delta", tank_volume_liters,
                 ec_dose_A_ml_, ec_dose_B_ml_, ec_dose_C_ml_);

        if (psm_) psm_->logEvent("EC_FEEDING", 0);
        transitionTo(ControllerState::EC_FEEDING);
    }
}

// ============================================================================
// EC feeding state handlers
// ============================================================================

void PlantOSController::handleEcProcessing() {
    // Yellow pulse - check EC reading and decide if feeding is needed
    if (!hal_) {
        transitionTo(ControllerState::ERROR);
        return;
    }

    // Guard: EC sensor must have a valid reading
    if (!hal_->hasECValue()) {
        ESP_LOGW(TAG, "[EC_PROCESSING] No EC reading available - skipping EC check");
        auto_ec_check_pending_ = false;
        transitionAfterEcSkipped();
        return;
    }

    // Guard: CalendarManager required for EC targets
    if (!calendar_manager_) {
        ESP_LOGD(TAG, "[EC_PROCESSING] No calendar manager - skipping EC check");
        auto_ec_check_pending_ = false;
        transitionAfterEcSkipped();
        return;
    }

    auto schedule = calendar_manager_->get_today_schedule();

    // Guard: EC feeding must be configured for today (ec_target == 0 means disabled)
    if (schedule.ec_target <= 0.0f) {
        ESP_LOGD(TAG, "[EC_PROCESSING] EC target not configured for day %d - skipping",
                 calendar_manager_->get_current_day());
        auto_ec_check_pending_ = false;
        transitionAfterEcSkipped();
        return;
    }

    // Guard: EC_MIN_INTERVAL_S (12 h) minimum interval between actual feedings
    if (hal_->hasTime() && last_ec_feeding_timestamp_ > 0) {
        int64_t current_ts = hal_->getCurrentTimestamp();
        int64_t elapsed_s = current_ts - last_ec_feeding_timestamp_;
        if (elapsed_s < EC_MIN_INTERVAL_S) {
            int64_t wait_s = EC_MIN_INTERVAL_S - elapsed_s;
            ESP_LOGI(TAG, "[EC_PROCESSING] skipped: last feeding %lld s ago, 12 h guard (%lld s left)",
                     (long long)elapsed_s, (long long)wait_s);
            auto_ec_check_pending_ = false;
            transitionAfterEcSkipped();
            return;
        }
    }

    float ec_current = hal_->readEC();  // µS/cm
    float ec_target = schedule.ec_target * ec_target_multiplier_;  // calendar target scaled by slider
    const dosing_math::EcWindow window = dosing_math::ecWindow(ec_target, schedule.ec_tolerance);
    float ec_min = window.min;
    float ec_max = window.max;
    const dosing_math::EcLevel ec_level = dosing_math::classifyEc(ec_current, ec_target, schedule.ec_tolerance);

    ESP_LOGI(TAG, "[EC_PROCESSING] EC %.1f uS/cm, target %.0f (calendar %.0f x %.2f) +-%.0f, window %.0f-%.0f",
             ec_current, ec_target, schedule.ec_target, ec_target_multiplier_,
             schedule.ec_tolerance, ec_min, ec_max);

    // Over-concentration: wait for natural evaporation to lower EC
    if (ec_level == dosing_math::EcLevel::ABOVE_RANGE) {
        ESP_LOGW(TAG, "[EC_PROCESSING] EC above window - no feeding, add plain water");
        status_logger_.updateAlertStatus("EC_HIGH", "EC over-concentration - add plain water or wait");
        auto_ec_check_pending_ = false;
        transitionAfterEcSkipped();
        return;
    }

    // EC in acceptable window - no feeding needed
    if (ec_level == dosing_math::EcLevel::IN_RANGE) {
        ESP_LOGD(TAG, "[EC_PROCESSING] EC in range [%.2f-%.2f] - no feeding needed", ec_min, ec_max);
        status_logger_.resolveAlert("EC_HIGH");
        auto_ec_check_pending_ = false;
        transitionAfterEcSkipped();
        return;
    }

    // EC below minimum - feeding required
    ESP_LOGI(TAG, "[EC_PROCESSING] EC below window - feeding");

    // Store initial EC and reset cycle state for K-factor calculation
    ec_pre_feeding_ = ec_current;
    ec_total_ml_per_L_ = 0.0f;
    ec_attempt_count_ = 0;
    ec_cycle_water_filled_ = false;

    // Log PSM event for crash recovery
    if (psm_) {
        psm_->logEvent("EC_FEEDING", 0);  // 0 = STARTED
    }

    transitionTo(ControllerState::EC_CALCULATING);
}

void PlantOSController::handleEcCalculating() {
    // Yellow fast blink - calculating EC-based nutrient doses
    if (!hal_ || !calendar_manager_) {
        ESP_LOGW(TAG, "[EC_CALCULATING] Missing dependencies - aborting");
        if (psm_) psm_->clearEvent();
        transitionTo(ControllerState::IDLE);
        return;
    }

    // Use current EC reading for delta (may differ from ec_pre_feeding_ on retries)
    float ec_current = hal_->hasECValue() ? hal_->readEC() : ec_pre_feeding_;
    auto schedule = calendar_manager_->get_today_schedule();
    float ec_target = schedule.ec_target * ec_target_multiplier_;  // calendar target scaled by slider
    float delta_ec = ec_target - ec_current;

    if (delta_ec <= 10.0f) {  // 0.01 mS/cm = 10 µS/cm
        ESP_LOGI(TAG, "[EC_CALCULATING] EC %.1f within 10 of target %.0f - no dose", ec_current, ec_target);
        if (psm_) psm_->clearEvent();
        transitionTo(ControllerState::IDLE);
        return;
    }

    // Tank volume for absolute dose calculation
    float tank_volume_L = hal_->getTankVolumeDelta();
    if (tank_volume_L <= 0.0f) {
        ESP_LOGW(TAG, "[EC_CALCULATING] Tank volume unavailable - using 10L fallback");
        tank_volume_L = 10.0f;
    }

    // Total dose in mL/L using adaptive K-factor (delta_ec / ec_K_feed_), split by the
    // A:B:C ratio from CalendarManager and capped at the calendar max dose per pump
    const dosing_math::EcDoses doses = dosing_math::calcEcDoses(
        delta_ec, ec_K_feed_, tank_volume_L, schedule.nutrient_A_ml_per_liter,
        schedule.nutrient_B_ml_per_liter, schedule.nutrient_C_ml_per_liter);
    float total_ml_per_L = doses.total_ml_per_L;

    if (!doses.valid) {
        ESP_LOGW(TAG, "[EC_CALCULATING] No nutrient ratios for day %d - skipping",
                 calendar_manager_->get_current_day());
        if (psm_) psm_->clearEvent();
        transitionTo(ControllerState::IDLE);
        return;
    }

    ec_dose_A_ml_ = doses.a_ml;
    ec_dose_B_ml_ = doses.b_ml;
    ec_dose_C_ml_ = doses.c_ml;

    // Accumulate mL/L for K-factor update after mixing
    ec_total_ml_per_L_ += doses.dosed_ml_per_L;

    ESP_LOGI(TAG, "[EC_CALCULATING] dEC %.1f uS/cm, K %.6f -> %.3f mL/L; A %.2f B %.2f C %.2f mL (%.1f L)",
             delta_ec, ec_K_feed_, total_ml_per_L, ec_dose_A_ml_, ec_dose_B_ml_, ec_dose_C_ml_, tank_volume_L);

    transitionTo(ControllerState::EC_FEEDING);
}

void PlantOSController::handleEcFeeding() {
    // Orange pulse - sequential nutrient pump A → B → C (EC-calculated doses)
    uint32_t elapsed = getStateElapsed();

    // Use state_counter_ as pump index (0=A, 1=B, 2=C, 3=done)
    const char* pump_name = nullptr;
    float dose_ml = 0.0f;

    if (state_counter_ == 0) {
        pump_name = NUTRIENT_PUMP_A;
        dose_ml = ec_dose_A_ml_;
    } else if (state_counter_ == 1) {
        pump_name = NUTRIENT_PUMP_B;
        dose_ml = ec_dose_B_ml_;
    } else if (state_counter_ == 2) {
        pump_name = NUTRIENT_PUMP_C;
        dose_ml = ec_dose_C_ml_;
    } else {
        // All pumps complete - accumulate mL/L for K-factor update, then mix
        if (hal_) {
            float tank_L = hal_->getTankVolumeDelta();
            if (tank_L > 0.0f) {
                float dosed_ml = ec_dose_A_ml_ + ec_dose_B_ml_ + ec_dose_C_ml_;
                ec_total_ml_per_L_ += dosed_ml / tank_L;
            }
        }
        if (is_mother_mix_) {
            is_mother_mix_ = false;
            ESP_LOGI(TAG, "[EC_FEEDING] Mother mix done");
            if (psm_) psm_->clearEvent();
            transitionTo(ControllerState::IDLE);
        } else {
            ESP_LOGI(TAG, "[EC_FEEDING] Dosing done (cycle total %.4f mL/L)", ec_total_ml_per_L_);
            transitionTo(ControllerState::EC_MIXING);
        }
        return;
    }

    // On entry for each pump: calculate duration and activate (only once per pump)
    if (!state_entry_executed_) {
        state_entry_executed_ = true;

        if (dose_ml > 0.0f && hal_) {
            float duration_sec = hal_->pumpflow(pump_name, dose_ml);
            ec_pump_duration_ms_ = static_cast<uint32_t>(duration_sec * 1000.0f);
            ESP_LOGI(TAG, "[EC_FEEDING] %s: %.2f mL = %.2f s", pump_name, dose_ml, duration_sec);
        } else {
            ec_pump_duration_ms_ = 0;
        }

        if (ec_pump_duration_ms_ > 0) {
            uint32_t duration_sec = (ec_pump_duration_ms_ + 999) / 1000;
            if (!requestPumpAdaptive(pump_name, true, duration_sec, false)) {
                ESP_LOGE(TAG, "[EC_FEEDING] %s rejected by SafetyGate - aborting EC feeding", pump_name);
                if (psm_) psm_->clearEvent();
                transitionTo(ControllerState::IDLE);
                return;
            }
            state_start_time_ = hal_->getSystemTime();
        } else {
            ESP_LOGD(TAG, "[EC_FEEDING] %s dose 0 - skipped", pump_name);
            state_counter_++;
            state_entry_executed_ = false;  // allow entry block to run for next pump
            state_start_time_ = hal_->getSystemTime();
        }
        return;
    }

    // Wait for pump duration + 200ms safety margin
    if (elapsed < ec_pump_duration_ms_ + 200) {
        return;
    }

    // Turn off pump explicitly
    if (safety_gate_) {
        safety_gate_->executeCommand(pump_name, false, 0);
    }
    ESP_LOGD(TAG, "[EC_FEEDING] %s done", pump_name);

    // Advance to next pump
    state_counter_++;
    state_entry_executed_ = false;  // allow entry block to run for next pump
    state_start_time_ = hal_->getSystemTime();
}

void PlantOSController::handleEcMixing() {
    // Blue pulse - 5 minute mixing after EC nutrient dosing
    // AirPump controlled externally (Shelly pattern); we just wait for mixing to complete
    static constexpr uint32_t EC_MIXING_DURATION_MS = 300000;  // 5 minutes

    uint32_t elapsed = getStateElapsed();

    if (!state_entry_executed_) {
        state_entry_executed_ = true;
    }

    if (elapsed >= EC_MIXING_DURATION_MS) {
        transitionTo(ControllerState::EC_MEASURING);
    }
}

void PlantOSController::handleEcMeasuring() {
    // Yellow pulse - re-measure EC after mixing, update K-factor, decide next step
    static constexpr uint32_t EC_SETTLE_MS = 30000;  // 30s settle time before reading

    uint32_t elapsed = getStateElapsed();

    if (!state_entry_executed_) {
        state_entry_executed_ = true;
        ec_attempt_count_++;
        uint8_t max_attempts = feeding_manual_mode_ ? FEEDING_MANUAL_MAX_CYCLES : MAX_EC_ATTEMPTS;
        ESP_LOGD(TAG, "[EC_MEASURING] Installment %u/%u - %us settle", (unsigned)ec_attempt_count_,
                 (unsigned)max_attempts, (unsigned)(EC_SETTLE_MS / 1000));
        return;
    }

    if (elapsed < EC_SETTLE_MS) {
        return;
    }

    if (!hal_->hasECValue()) {
        ESP_LOGW(TAG, "[EC_MEASURING] No EC reading available - aborting cycle");
        feeding_manual_mode_ = false;
        if (psm_) psm_->clearEvent();
        transitionTo(ControllerState::IDLE);
        return;
    }

    float ec_after = hal_->readEC();
    ESP_LOGI(TAG, "[EC_MEASURING] EC %.1f uS/cm after installment %u (cycle start %.1f, +%.1f)",
             ec_after, (unsigned)ec_attempt_count_, ec_pre_feeding_, ec_after - ec_pre_feeding_);

    // Update adaptive K-factor based on observed EC response
    updateEcKFactor(ec_pre_feeding_, ec_after, ec_total_ml_per_L_);

    // Record timestamp to enforce the 12 h (EC_MIN_INTERVAL_S) minimum interval between automatic feedings
    if (hal_->hasTime()) {
        last_ec_feeding_timestamp_ = hal_->getCurrentTimestamp();
    }

    // Get target to check if we're in range
    float ec_min = 0.0f;
    float ec_target = 0.0f;
    if (calendar_manager_) {
        auto schedule = calendar_manager_->get_today_schedule();
        ec_target = schedule.ec_target * ec_target_multiplier_;  // calendar target scaled by slider
        ec_min = ec_target - schedule.ec_tolerance;
    }

    uint8_t max_attempts = feeding_manual_mode_ ? FEEDING_MANUAL_MAX_CYCLES : MAX_EC_ATTEMPTS;
    bool in_range = (ec_min > 0.0f) && (ec_after >= ec_min);
    bool max_attempts_reached = (ec_attempt_count_ >= max_attempts);

    if (in_range) {
        ESP_LOGI(TAG, "[EC_MEASURING] EC target reached (>= %.1f)", ec_min);
        feeding_manual_mode_ = false;
        status_logger_.resolveAlert("EC_HIGH");
        status_logger_.resolveAlert("EC_LOW");
    } else if (max_attempts_reached) {
        ESP_LOGW(TAG, "[EC_MEASURING] Max installments (%u) reached, EC still below %.1f (target %.0f)",
                 (unsigned)max_attempts, ec_min, ec_target);
        feeding_manual_mode_ = false;
        status_logger_.updateAlertStatus("EC_LOW",
            "EC below target after max feeding installments - check nutrient supply");
    } else if (feeding_manual_mode_) {
        // Manual feeding: use same pre-calculated installment doses (not K-factor recalculation)
        ESP_LOGI(TAG, "[EC_MEASURING] EC below %.1f - next installment", ec_min);
        transitionTo(ControllerState::EC_FEEDING);
        return;
    } else {
        // Automated EC correction: retry with K-factor recalculated dose
        ESP_LOGI(TAG, "[EC_MEASURING] EC below %.1f - recalculating dose (attempt %u/%u)", ec_min,
                 (unsigned)ec_attempt_count_, (unsigned)MAX_EC_ATTEMPTS);
        transitionTo(ControllerState::EC_CALCULATING);
        return;
    }

    // Always check pH after EC feeding (nutrients shift pH)
    ESP_LOGI(TAG, "[EC_MEASURING] EC cycle done - pH check next (300 s settle)");

    resetPhCycle();

    // Use extended stabilization: nutrients need time to distribute before accurate pH reading
    ph_post_feed_stabilize_ = true;

    if (psm_) {
        psm_->clearEvent();
        psm_->logEvent("PH_CORRECTION", 0);  // 0 = STARTED
    }

    transitionTo(ControllerState::PH_PROCESSING);
}

void PlantOSController::transitionAfterEcSkipped() {
    // Post-fill sequencing (docs/CONTROL_LOGIC.md): if auto pH correction is pending,
    // transition to PH_PROCESSING; otherwise return to IDLE.
    if (auto_ph_correction_pending_) {
        ESP_LOGD(TAG, "[EC_PROCESSING] pH check pending");
        auto_ph_correction_pending_ = false;

        resetPhCycle();

        if (psm_) {
            psm_->clearEvent();
            psm_->logEvent("PH_CORRECTION", 0);  // 0 = STARTED
        }

        transitionTo(ControllerState::PH_PROCESSING);
    } else {
        transitionTo(ControllerState::IDLE);
    }
}

void PlantOSController::updateEcKFactor(float ec_before, float ec_after, float ml_per_L_total) {
    float ec_delta = ec_after - ec_before;

    // Guard: need a meaningful positive EC rise
    if (ec_delta < 50.0f) {  // 0.05 mS/cm = 50 µS/cm
        ESP_LOGD(TAG, "EC K-factor update skipped: delta too small (%.1f uS/cm < 50)", ec_delta);
        return;
    }
    if (ml_per_L_total < 0.01f) {
        ESP_LOGD(TAG, "EC K-factor update skipped: dose too small (%.4f mL/L < 0.01)", ml_per_L_total);
        return;
    }
    if (ec_cycle_water_filled_) {
        ESP_LOGD(TAG, "EC K-factor update skipped: water fill happened during cycle (dilution)");
        return;
    }

    // K_observed = dose / EC_rise (mL/L per µS/cm), clamped; K_new = EMA, clamped again
    const dosing_math::KUpdate k = dosing_math::ecKUpdate(ml_per_L_total, ec_delta, ec_K_feed_,
                                                          EC_K_EMA_ALPHA, EC_K_MIN_CLAMP, EC_K_MAX_CLAMP);
    float K_observed = k.observed;
    float K_new = k.updated;

    ESP_LOGI(TAG, "EC K update: observed %.6f, new %.6f (was %.6f); EC %.1f->%.1f (d %.1f), %.4f mL/L",
             K_observed, K_new, ec_K_feed_, ec_before, ec_after, ec_delta, ml_per_L_total);

    ec_K_feed_ = K_new;

    // Persist to NVS
    if (psm_) {
        psm_->saveFloat(NVS_KEY_EC_K, ec_K_feed_);
        ESP_LOGD(TAG, "EC K-factor saved to NVS: %.4f", ec_K_feed_);
    }
}

// ============================================================================
// EC calibration
// ============================================================================

void PlantOSController::handleEcCalibrating() {
    uint32_t now = hal_->getSystemTime();
    uint32_t step_elapsed = now - ec_calib_step_start_time_;

    switch (ec_calib_step_) {
        case ECCalibStep::RINSE_PROMPT:
            if (!ec_calib_prompt_shown_) {
                ec_calib_prompt_shown_ = true;
                ESP_LOGW(TAG, "EC calibration 1/4: take the probe out of the tank and rinse it (30 s)");
            }
            if (step_elapsed >= EC_CALIB_PROMPT_DURATION) {
                ec_calib_step_ = ECCalibStep::DIP_PROMPT;
                ec_calib_step_start_time_ = now;
                ec_calib_prompt_shown_ = false;
            }
            break;

        case ECCalibStep::DIP_PROMPT:
            if (!ec_calib_prompt_shown_) {
                ec_calib_prompt_shown_ = true;
                ESP_LOGW(TAG, "EC calibration 2/4: dip the probe into %.1f uS/cm solution (30 s)", ec_calib_target_);
                ec_calib_readings_.clear();
            }
            if (step_elapsed >= EC_CALIB_PROMPT_DURATION) {
                ec_calib_step_ = ECCalibStep::STABILIZING;
                ec_calib_step_start_time_ = now;
                ec_calib_last_reading_time_ = 0;
                ESP_LOGI(TAG, "EC calibration 3/4: collecting %u readings at 1 s", (unsigned)EC_CALIB_MIN_READINGS);
            }
            break;

        case ECCalibStep::STABILIZING: {
            // Collect one reading per second
            if (now - ec_calib_last_reading_time_ >= EC_CALIB_READING_INTERVAL) {
                ec_calib_last_reading_time_ = now;
                float reading = hal_->readEC();

                if (reading <= 0.0f) {
                    ESP_LOGW(TAG, "[EC_CALIB] Zero/negative EC reading - probe disconnected?");
                } else {
                    ec_calib_readings_.push_back(reading);
                    size_t n = ec_calib_readings_.size();
                    ESP_LOGD(TAG, "EC calib: %u readings (latest %.1f uS/cm)", (unsigned)n, reading);
                }
            }

            // Check if we have enough readings
            if (ec_calib_readings_.size() >= EC_CALIB_MIN_READINGS) {
                if (checkEcCalibrationStability()) {
                    ec_calib_step_ = ECCalibStep::CALCULATING;
                    ec_calib_step_start_time_ = now;
                } else {
                    // Discard oldest 10 readings and keep collecting
                    ESP_LOGW(TAG, "[EC_CALIB] Readings unstable - discarding oldest 10, continuing...");
                    if (ec_calib_readings_.size() > 10) {
                        ec_calib_readings_.erase(ec_calib_readings_.begin(),
                                                  ec_calib_readings_.begin() + 10);
                    }
                }
            }
            break;
        }

        case ECCalibStep::CALCULATING: {
            // Calculate mean of collected readings
            float sum = 0.0f;
            for (float r : ec_calib_readings_) sum += r;
            float mean = sum / ec_calib_readings_.size();

            if (mean <= 0.0f) {
                ESP_LOGE(TAG, "[EC_CALIB] Mean EC is zero - wiring fault?");
                status_logger_.updateAlertStatus("EC_CALIB_ERROR",
                    "EC measured as 0 uS/cm - check probe wiring");
                transitionTo(ControllerState::ERROR);
                ec_calib_step_ = ECCalibStep::IDLE;
                return;
            }

            float factor = ec_calib_target_ / mean;
            ESP_LOGI(TAG, "EC calibration: measured %.1f, target %.1f uS/cm -> factor %.4f",
                     mean, ec_calib_target_, factor);

            if (factor < EC_CALIB_FACTOR_MIN || factor > EC_CALIB_FACTOR_MAX) {
                ESP_LOGE(TAG, "[EC_CALIB] Factor %.4f out of valid range [%.1f-%.1f]",
                         factor, EC_CALIB_FACTOR_MIN, EC_CALIB_FACTOR_MAX);
                status_logger_.updateAlertStatus("EC_CALIB_ERROR",
                    "Calibration factor out of range - wrong solution or probe fault?");
                transitionTo(ControllerState::ERROR);
                ec_calib_step_ = ECCalibStep::IDLE;
                return;
            }

            if (!hal_->setECCalibrationFactor(factor)) {
                ESP_LOGE(TAG, "[EC_CALIB] Failed to save calibration factor via HAL");
                status_logger_.updateAlertStatus("EC_CALIB_ERROR",
                    "Failed to persist calibration factor - TDS sensor not wired to HAL");
                transitionTo(ControllerState::ERROR);
                ec_calib_step_ = ECCalibStep::IDLE;
                return;
            }

            ESP_LOGI(TAG, "EC calibration factor %.4f saved", factor);
            ec_calib_step_ = ECCalibStep::STORE_PROMPT;
            ec_calib_step_start_time_ = now;
            ec_calib_prompt_shown_ = false;
            break;
        }

        case ECCalibStep::STORE_PROMPT:
            if (!ec_calib_prompt_shown_) {
                ec_calib_prompt_shown_ = true;
                ESP_LOGW(TAG, "EC calibration 4/4: rinse the probe, put it back (factor %.4f, IDLE in 30 s)",
                         hal_->getECCalibrationFactor());
            }
            if (step_elapsed >= EC_CALIB_PROMPT_DURATION) {
                ec_calib_step_ = ECCalibStep::COMPLETE;
            }
            break;

        case ECCalibStep::COMPLETE:
            ec_calib_step_ = ECCalibStep::IDLE;
            // Persist calibration timestamp so the 30-day reminder can track it
            if (psm_ && hal_->hasTime()) {
                last_ec_calibration_ts_ = hal_->getCurrentTimestamp();
                psm_->saveInt32(NVS_KEY_EC_CALIB_TS, (int32_t)last_ec_calibration_ts_);
            }
            ESP_LOGI(TAG, "EC calibration complete");
            transitionTo(ControllerState::IDLE);
            break;

        case ECCalibStep::IDLE:
            // Should not happen in this state
            transitionTo(ControllerState::IDLE);
            break;
    }
}

bool PlantOSController::checkEcCalibrationStability() {
    if (ec_calib_readings_.size() < EC_CALIB_MIN_READINGS) return false;

    const dosing_math::ReadingStats stats = dosing_math::readingStats(ec_calib_readings_);
    float mean = stats.mean;

    if (mean <= 0.0f) return false;

    float std_dev = stats.std_dev;
    float cv = stats.cv;

    bool stable = cv < EC_CALIB_VARIANCE_THRESHOLD;
    ESP_LOGI(TAG, "EC calib stability: mean %.1f, std %.1f, CV %.3f (max %.2f) -> %s",
             mean, std_dev, cv, EC_CALIB_VARIANCE_THRESHOLD, stable ? "stable" : "NOT stable");
    return stable;
}

void PlantOSController::resetEcCalibration() {
    if (hal_->resetECCalibrationFactor()) {
        ESP_LOGI(TAG, "EC calibration factor reset to 1.0 (factory default)");
    } else {
        ESP_LOGW(TAG, "EC calibration reset failed - TDS sensor not configured in HAL");
    }
}
} // namespace plantos_controller
