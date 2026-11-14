// pH dosing math of the controller.
// Real code under test: components/plantos_controller/controller.cpp
//   calculateAcidDoseML, updatePhKFactor, calculatePhMixingDuration,
//   calculateRobustPhAverage, handlePhCalculating, handlePhProcessing
#include "minitest.h"
#include "fixture.h"

using plantos_controller::ControllerState;

// ---------------------------------------------------------------------------
// calculateAcidDoseML: dose = (pH - 5.85) * tank_L * K, clamped to [0.5, 10] mL
// ---------------------------------------------------------------------------

TEST(acid_dose, formula_with_production_tank) {
  ControllerFixture f;  // tank delta volume 27.4 L
  f.ctrl.ph_K_ = 0.07f;
  // (6.50 - 5.85) * 27.4 * 0.07 = 1.2467
  CHECK_NEAR(f.ctrl.calculateAcidDoseML(6.5f), 1.2467f, 1e-3);
  // (7.00 - 5.85) * 27.4 * 0.07 = 2.2057
  CHECK_NEAR(f.ctrl.calculateAcidDoseML(7.0f), 2.2057f, 1e-3);
}

TEST(acid_dose, zero_at_or_below_fixed_target_5_85) {
  ControllerFixture f;
  f.ctrl.ph_K_ = 0.07f;
  CHECK_EQ(f.ctrl.calculateAcidDoseML(5.85f), 0.0f);
  CHECK_EQ(f.ctrl.calculateAcidDoseML(5.5f), 0.0f);
  CHECK_EQ(f.ctrl.calculateAcidDoseML(0.0f), 0.0f);
}

TEST(acid_dose, tiny_delta_is_raised_to_minimum_dose) {
  ControllerFixture f;
  f.ctrl.ph_K_ = 0.07f;
  // (5.90 - 5.85) * 27.4 * 0.07 = 0.096 mL -> clamped UP to 0.5 mL.
  // Any pH above 5.85 therefore doses at least 0.5 mL; the "dose too small" branch in
  // handlePhCalculating (dose < 0.5) can only be hit by the 0.0 return.
  CHECK_NEAR(f.ctrl.calculateAcidDoseML(5.9f), 0.5f, 1e-6);
  CHECK_NEAR(f.ctrl.calculateAcidDoseML(5.86f), 0.5f, 1e-6);
}

TEST(acid_dose, clamped_to_maximum_10_ml) {
  ControllerFixture f;
  f.ctrl.ph_K_ = 0.5f;
  // (9.0 - 5.85) * 27.4 * 0.5 = 43.2 -> 10
  CHECK_NEAR(f.ctrl.calculateAcidDoseML(9.0f), 10.0f, 1e-6);
}

TEST(acid_dose, uses_delta_volume_not_total_volume) {
  ControllerFixture f;
  f.ctrl.ph_K_ = 0.1f;
  f.hal.tank_volume_delta = 20.0f;
  f.hal.total_tank_volume = 99.0f;
  // (6.85 - 5.85) * 20 * 0.1 = 2.0
  CHECK_NEAR(f.ctrl.calculateAcidDoseML(6.85f), 2.0f, 1e-3);
}

TEST(acid_dose, without_hal_assumes_10_liters) {
  plantos_controller::PlantOSController ctrl;
  ctrl.ph_K_ = 0.1f;
  // (6.85 - 5.85) * 10 * 0.1 = 1.0
  CHECK_NEAR(ctrl.calculateAcidDoseML(6.85f), 1.0f, 1e-3);
}

TEST(acid_dose, k_factor_defaults) {
  // Member default is 0.25; setup() then loads "PhK" from NVS with fallback 0.07.
  plantos_controller::PlantOSController fresh;
  CHECK_NEAR(fresh.get_ph_k_factor(), 0.25f, 1e-6);
  ControllerFixture f;
  f.ctrl.setup();
  CHECK_NEAR(f.ctrl.get_ph_k_factor(), 0.07f, 1e-6);
  CHECK_NEAR(f.ctrl.ec_K_feed_, 0.00015f, 1e-9);
}

// ---------------------------------------------------------------------------
// updatePhKFactor: K_obs = mL / (dPH * L), clamp [0.01, 0.5], EMA alpha 0.2
// ---------------------------------------------------------------------------

TEST(ph_k_factor, ema_update_persist_and_callback) {
  ControllerFixture f;
  f.ctrl.ph_K_ = 0.07f;
  float reported = -1.0f;
  f.ctrl.set_ph_k_update_callback([&](float k) { reported = k; });
  // K_obs = 1.5 / (0.6 * 27.4) = 0.091241 ; K_new = 0.2 * 0.091241 + 0.8 * 0.07 = 0.074248
  f.ctrl.updatePhKFactor(6.5f, 5.9f, 1.5f);
  CHECK_NEAR(f.ctrl.ph_K_, 0.074248f, 1e-5);
  CHECK_NEAR(reported, 0.074248f, 1e-5);
  CHECK_NEAR(f.psm.loadFloat("PhK", -1.0f), 0.074248f, 1e-5);
}

TEST(ph_k_factor, observed_k_is_clamped_before_ema) {
  ControllerFixture f;
  f.ctrl.ph_K_ = 0.07f;
  // K_obs = 10 / (0.1 * 27.4) = 3.65 -> 0.5 ; K_new = 0.1 + 0.056 = 0.156
  f.ctrl.updatePhKFactor(6.5f, 6.4f, 10.0f);
  CHECK_NEAR(f.ctrl.ph_K_, 0.156f, 1e-5);

  f.ctrl.ph_K_ = 0.07f;
  // K_obs = 0.2 / (2.0 * 27.4) = 0.00365 -> 0.01 ; K_new = 0.002 + 0.056 = 0.058
  f.ctrl.updatePhKFactor(8.0f, 6.0f, 0.2f);
  CHECK_NEAR(f.ctrl.ph_K_, 0.058f, 1e-5);
}

TEST(ph_k_factor, result_is_not_clamped_after_ema) {
  // Unlike the EC K-factor there is no final clamp: a K set outside [0.01, 0.5]
  // (e.g. via the slider) only decays towards the range.
  ControllerFixture f;
  f.ctrl.ph_K_ = 1.0f;
  f.ctrl.updatePhKFactor(6.5f, 6.4f, 10.0f);  // K_obs clamped to 0.5
  CHECK_NEAR(f.ctrl.ph_K_, 0.9f, 1e-5);
}

TEST(ph_k_factor, guards_skip_the_update) {
  ControllerFixture f;
  f.ctrl.ph_K_ = 0.07f;
  int callbacks = 0;
  f.ctrl.set_ph_k_update_callback([&](float) { callbacks++; });

  f.ctrl.updatePhKFactor(6.50f, 6.46f, 1.5f);  // delta 0.04 <= 0.05
  f.ctrl.updatePhKFactor(6.0f, 6.5f, 1.5f);    // pH went up
  f.ctrl.updatePhKFactor(6.5f, 5.9f, 0.1f);    // dosed <= 0.1 mL
  f.ctrl.updatePhKFactor(6.5f, 5.9f, 0.0f);

  f.ctrl.ph_cycle_water_filled_ = true;
  f.ctrl.updatePhKFactor(6.5f, 5.9f, 1.5f);
  f.ctrl.ph_cycle_water_filled_ = false;

  f.ctrl.ph_cycle_aborted_ = true;
  f.ctrl.updatePhKFactor(6.5f, 5.9f, 1.5f);
  f.ctrl.ph_cycle_aborted_ = false;

  CHECK_EQ(f.ctrl.ph_K_, 0.07f);
  CHECK_EQ(callbacks, 0);
  CHECK_EQ(f.psm.loadFloat("PhK", -1.0f), -1.0f);  // nothing persisted

  f.ctrl.updatePhKFactor(6.50f, 6.44f, 1.5f);  // delta 0.06 > 0.05 -> learns
  CHECK_EQ(callbacks, 1);
}

TEST(ph_k_factor, without_hal_assumes_10_liters) {
  plantos_controller::PlantOSController ctrl;
  ctrl.ph_K_ = 0.1f;
  // K_obs = 2.0 / (0.5 * 10) = 0.4 ; K_new = 0.08 + 0.08 = 0.16
  ctrl.updatePhKFactor(6.5f, 6.0f, 2.0f);
  CHECK_NEAR(ctrl.ph_K_, 0.16f, 1e-5);
}

// ---------------------------------------------------------------------------
// calculatePhMixingDuration: 8344.07 ms/L * V + 15814.9 ms, clamped to [30 s, 600 s]
// ---------------------------------------------------------------------------

TEST(ph_mixing, linear_in_tank_volume) {
  ControllerFixture f;
  f.hal.tank_volume_delta = 27.4f;  // production
  CHECK_NEAR(f.ctrl.calculatePhMixingDuration(), 244442, 2);
  f.hal.tank_volume_delta = 10.0f;
  CHECK_NEAR(f.ctrl.calculatePhMixingDuration(), 99255, 2);
  f.hal.tank_volume_delta = 50.0f;
  CHECK_NEAR(f.ctrl.calculatePhMixingDuration(), 433018, 2);
}

TEST(ph_mixing, clamped_to_30s_and_600s) {
  ControllerFixture f;
  f.hal.tank_volume_delta = 0.0f;
  CHECK_EQ(f.ctrl.calculatePhMixingDuration(), 30000u);
  f.hal.tank_volume_delta = 1.7f;  // formula gives 29999.8 -> clamped up
  CHECK_EQ(f.ctrl.calculatePhMixingDuration(), 30000u);
  f.hal.tank_volume_delta = 70.0f;  // formula gives 599899.8: just below the cap
  CHECK_NEAR(f.ctrl.calculatePhMixingDuration(), 599899, 2);
  f.hal.tank_volume_delta = 71.0f;
  CHECK_EQ(f.ctrl.calculatePhMixingDuration(), 600000u);
  f.hal.tank_volume_delta = 500.0f;
  CHECK_EQ(f.ctrl.calculatePhMixingDuration(), 600000u);
}

TEST(ph_mixing, without_hal_assumes_10_liters) {
  plantos_controller::PlantOSController ctrl;
  CHECK_NEAR(ctrl.calculatePhMixingDuration(), 99255, 2);
}

// ---------------------------------------------------------------------------
// calculateRobustPhAverage (trimmed mean over ph_readings_)
// ---------------------------------------------------------------------------

TEST(ph_average, empty_and_single) {
  ControllerFixture f;
  CHECK_EQ(f.ctrl.calculateRobustPhAverage(), 0.0f);
  f.ctrl.ph_readings_ = {6.3f};
  CHECK_EQ(f.ctrl.calculateRobustPhAverage(), 6.3f);
}

TEST(ph_average, two_readings_plain_mean) {
  ControllerFixture f;
  f.ctrl.ph_readings_ = {6.0f, 7.0f};
  CHECK_NEAR(f.ctrl.calculateRobustPhAverage(), 6.5f, 1e-5);
}

TEST(ph_average, three_to_nineteen_readings_drop_one_from_each_end) {
  ControllerFixture f;
  f.ctrl.ph_readings_ = {9.0f, 6.1f, 1.0f};
  CHECK_NEAR(f.ctrl.calculateRobustPhAverage(), 6.1f, 1e-5);  // median
  f.ctrl.ph_readings_ = {6.2f, 1.0f, 6.0f, 9.0f, 6.1f};
  CHECK_NEAR(f.ctrl.calculateRobustPhAverage(), 6.1f, 1e-5);
  // 10 readings: 10 % = 1 from each end
  f.ctrl.ph_readings_ = {0.0f, 6.0f, 6.0f, 6.0f, 6.0f, 6.2f, 6.2f, 6.2f, 6.2f, 14.0f};
  CHECK_NEAR(f.ctrl.calculateRobustPhAverage(), 6.1f, 1e-5);
  // 19 readings: still only 1 from each end (19 / 10 = 1), so a second outlier leaks in
  f.ctrl.ph_readings_.assign(19, 6.0f);
  f.ctrl.ph_readings_[0] = 14.0f;
  f.ctrl.ph_readings_[1] = 14.0f;
  CHECK_NEAR(f.ctrl.calculateRobustPhAverage(), (16 * 6.0f + 14.0f) / 17.0f, 1e-4);
}

TEST(ph_average, twenty_readings_drop_two_from_each_end) {
  ControllerFixture f;
  f.ctrl.ph_readings_.assign(20, 6.0f);
  f.ctrl.ph_readings_[3] = 14.0f;
  f.ctrl.ph_readings_[7] = 13.0f;
  f.ctrl.ph_readings_[11] = 0.0f;
  f.ctrl.ph_readings_[15] = 1.0f;
  CHECK_NEAR(f.ctrl.calculateRobustPhAverage(), 6.0f, 1e-5);
}

TEST(ph_average, does_not_modify_the_buffer) {
  ControllerFixture f;
  f.ctrl.ph_readings_ = {9.0f, 6.1f, 1.0f};
  f.ctrl.calculateRobustPhAverage();
  CHECK_EQ(f.ctrl.ph_readings_[0], 9.0f);
  CHECK_EQ(f.ctrl.ph_readings_.size(), 3u);
}

// ---------------------------------------------------------------------------
// handlePhProcessing: periodic check against the HAL range (plantos_hal ph_min/ph_max)
// ---------------------------------------------------------------------------

TEST(ph_processing, decision_uses_hal_range) {
  ControllerFixture f;  // HAL range 5.5 .. 6.2 as in plantOS.yaml
  auto run = [&](float ph) {
    f.force_state(ControllerState::PH_PROCESSING);
    f.hal.ph = ph;
    f.ctrl.handlePhProcessing();
    return f.ctrl.getCurrentState();
  };
  CHECK_EQ(run(5.5f), ControllerState::IDLE);
  CHECK_EQ(run(6.2f), ControllerState::IDLE);
  CHECK_EQ(run(6.21f), ControllerState::PH_MEASURING);
  CHECK_EQ(std::string(f.psm.getLastEvent().eventID), std::string("PH_CORRECTION"));
  CHECK_EQ(run(5.49f), ControllerState::IDLE);  // too low: no base pump, nothing happens

  f.hal.has_ph = false;
  CHECK_EQ(run(9.0f), ControllerState::IDLE);
}

TEST(ph_processing, range_differs_from_calendar_range) {
  // Day 60 of the calendar allows 6.0 .. 6.4, the HAL range stays 5.5 .. 6.2:
  // pH 6.3 starts a correction cycle here, and PH_CALCULATING then finds it in range.
  ControllerFixture f;
  f.cal.set_current_day(60);
  f.force_state(ControllerState::PH_PROCESSING);
  f.hal.ph = 6.3f;
  f.ctrl.handlePhProcessing();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::PH_MEASURING);

  f.force_state(ControllerState::PH_CALCULATING);
  f.ctrl.ph_current_ = 6.3f;
  f.ctrl.handlePhCalculating();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
}

// ---------------------------------------------------------------------------
// handlePhCalculating: decision against the calendar range, dose -> pump time
// ---------------------------------------------------------------------------

TEST(ph_calculating, high_ph_computes_dose_and_duration) {
  ControllerFixture f;  // day 1: 5.8 .. 6.2 ; AcidPump 2.35 mL/s
  f.ctrl.ph_K_ = 0.07f;
  f.force_state(ControllerState::PH_CALCULATING);
  f.ctrl.ph_current_ = 6.5f;
  f.ctrl.handlePhCalculating();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::PH_INJECTING);
  CHECK_NEAR(f.ctrl.ph_dose_ml_, 1.2467f, 1e-3);
  // 1.2467 mL / 2.35 mL/s = 0.5305 s
  CHECK_NEAR(f.ctrl.ph_dose_duration_ms_, 530, 1);
  CHECK_EQ(f.ctrl.ph_attempt_count_, 1u);
  CHECK_NEAR(f.ctrl.ph_cycle_start_ph_, 6.5f, 1e-6);
}

TEST(ph_calculating, in_range_and_too_low_return_to_idle) {
  ControllerFixture f;
  auto run = [&](float ph) {
    f.force_state(ControllerState::PH_CALCULATING);
    f.ctrl.ph_current_ = ph;
    f.ctrl.handlePhCalculating();
    return f.ctrl.getCurrentState();
  };
  CHECK_EQ(run(5.8f), ControllerState::IDLE);
  CHECK_EQ(run(6.2f), ControllerState::IDLE);
  CHECK_EQ(run(5.7f), ControllerState::IDLE);
  CHECK_EQ(f.ctrl.ph_attempt_count_, 0u);
  CHECK_EQ(f.hal.calls.size(), 0u);
}

TEST(ph_calculating, without_calendar_uses_5_5_to_6_5) {
  ControllerFixture f(false);
  f.ctrl.ph_K_ = 0.07f;
  auto run = [&](float ph) {
    f.force_state(ControllerState::PH_CALCULATING);
    f.ctrl.ph_current_ = ph;
    f.ctrl.handlePhCalculating();
    return f.ctrl.getCurrentState();
  };
  CHECK_EQ(run(6.5f), ControllerState::IDLE);
  CHECK_EQ(run(6.6f), ControllerState::PH_INJECTING);
}

TEST(ph_calculating, stops_after_five_attempts) {
  ControllerFixture f;
  f.ctrl.ph_K_ = 0.07f;
  f.force_state(ControllerState::PH_CALCULATING);
  f.ctrl.ph_current_ = 6.5f;
  f.ctrl.ph_attempt_count_ = 5;
  f.ctrl.handlePhCalculating();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  CHECK_EQ(f.ctrl.ph_attempt_count_, 5u);
}

TEST(ph_calculating, learns_k_when_back_in_range_after_dosing) {
  ControllerFixture f;
  f.ctrl.ph_K_ = 0.07f;
  f.force_state(ControllerState::PH_CALCULATING);
  f.ctrl.ph_current_ = 5.9f;
  f.ctrl.ph_attempt_count_ = 1;
  f.ctrl.ph_cycle_start_ph_ = 6.5f;
  f.ctrl.ph_cycle_total_ml_ = 1.5f;
  f.ctrl.handlePhCalculating();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  CHECK_NEAR(f.ctrl.ph_K_, 0.074248f, 1e-5);  // same numbers as ph_k_factor.ema_update_*
}
