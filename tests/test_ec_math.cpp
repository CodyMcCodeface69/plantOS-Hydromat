// EC feeding math of the controller.
// Real code under test: components/plantos_controller/controller.cpp
//   updateEcKFactor, handleEcProcessing (trigger window), handleEcCalculating (dose split),
//   handleFeeding (manual installments), handleEcFeeding (dose -> pump time), handleEcMeasuring,
//   startMotherNutrients
#include "minitest.h"
#include "fixture.h"

using plantos_controller::ControllerState;

static bool has_active_alert(ControllerFixture &f, const std::string &type) {
  for (auto &a : f.ctrl.status_logger_.getActiveAlerts())
    if (a.type == type) return true;
  return false;
}

// Run EC_FEEDING until it leaves the state, 10 ms per tick.
static void run_ec_feeding(ControllerFixture &f) {
  for (int i = 0; i < 100000 && f.ctrl.getCurrentState() == ControllerState::EC_FEEDING; i++) {
    f.ctrl.handleEcFeeding();
    f.gate.loop();
    host::advance_millis(10);
  }
}
// Milliseconds a pump was ON according to the recorded HAL calls (first ON -> first OFF).
static int on_time_ms(const FakeHAL &hal, const std::string &pump) {
  int on = -1;
  for (auto &c : hal.calls) {
    if (c.kind != "pump" || c.id != pump) continue;
    if (c.state && on < 0) on = c.at_ms;
    if (!c.state && on >= 0) return static_cast<int>(c.at_ms) - on;
  }
  return -1;
}

// ---------------------------------------------------------------------------
// updateEcKFactor: K_obs = (mL/L) / dEC, clamp [0.00002, 0.0005], EMA alpha 0.2, final clamp
// ---------------------------------------------------------------------------

TEST(ec_k_factor, ema_update_and_persist) {
  ControllerFixture f;
  f.ctrl.ec_K_feed_ = 0.00015f;
  // K_obs = 0.04 / 200 = 0.0002 ; K_new = 0.2 * 0.0002 + 0.8 * 0.00015 = 0.00016
  f.ctrl.updateEcKFactor(800.0f, 1000.0f, 0.04f);
  CHECK_NEAR(f.ctrl.ec_K_feed_, 0.00016f, 1e-8);
  CHECK_NEAR(f.psm.loadFloat("EcKv2", -1.0f), 0.00016f, 1e-8);
}

TEST(ec_k_factor, observed_k_is_clamped_before_ema) {
  ControllerFixture f;
  f.ctrl.ec_K_feed_ = 0.00015f;
  // K_obs = 1.0 / 100 = 0.01 -> 0.0005 ; K_new = 0.0001 + 0.00012 = 0.00022
  f.ctrl.updateEcKFactor(800.0f, 900.0f, 1.0f);
  CHECK_NEAR(f.ctrl.ec_K_feed_, 0.00022f, 1e-8);

  f.ctrl.ec_K_feed_ = 0.00015f;
  // K_obs = 0.01 / 1000 = 0.00001 -> 0.00002 ; K_new = 0.000004 + 0.00012 = 0.000124
  f.ctrl.updateEcKFactor(500.0f, 1500.0f, 0.01f);
  CHECK_NEAR(f.ctrl.ec_K_feed_, 0.000124f, 1e-8);
}

TEST(ec_k_factor, result_is_clamped_after_ema) {
  ControllerFixture f;
  f.ctrl.ec_K_feed_ = 0.001f;  // outside the valid range
  f.ctrl.updateEcKFactor(800.0f, 900.0f, 1.0f);  // 0.2 * 0.0005 + 0.8 * 0.001 = 0.0009 -> 0.0005
  CHECK_NEAR(f.ctrl.ec_K_feed_, 0.0005f, 1e-9);
}

TEST(ec_k_factor, guards_skip_the_update) {
  ControllerFixture f;
  f.ctrl.ec_K_feed_ = 0.00015f;
  f.ctrl.updateEcKFactor(800.0f, 849.0f, 0.04f);   // rise 49 < 50 uS/cm
  f.ctrl.updateEcKFactor(800.0f, 700.0f, 0.04f);   // EC fell
  f.ctrl.updateEcKFactor(800.0f, 1000.0f, 0.009f); // dose < 0.01 mL/L
  f.ctrl.ec_cycle_water_filled_ = true;
  f.ctrl.updateEcKFactor(800.0f, 1000.0f, 0.04f);
  f.ctrl.ec_cycle_water_filled_ = false;
  CHECK_EQ(f.ctrl.ec_K_feed_, 0.00015f);
  CHECK_EQ(f.psm.loadFloat("EcKv2", -1.0f), -1.0f);

  f.ctrl.updateEcKFactor(800.0f, 850.0f, 0.01f);   // exactly 50 uS/cm and 0.01 mL/L are accepted
  // K_obs = 0.01 / 50 = 0.0002 ; K_new = 0.00016
  CHECK_NEAR(f.ctrl.ec_K_feed_, 0.00016f, 1e-8);
}

// ---------------------------------------------------------------------------
// handleEcProcessing: feed only below (target - tol); alarm above (target + 1.5 tol)
// Day 1: target 500, tolerance 100 uS/cm -> window [400, 650]
// ---------------------------------------------------------------------------

static ControllerState ec_processing(ControllerFixture &f, float ec) {
  f.force_state(ControllerState::EC_PROCESSING);
  f.hal.ec = ec;
  f.ctrl.handleEcProcessing();
  return f.ctrl.getCurrentState();
}

TEST(ec_processing, trigger_window_day_1) {
  ControllerFixture f;
  CHECK_EQ(ec_processing(f, 399.0f), ControllerState::EC_CALCULATING);
  CHECK_NEAR(f.ctrl.ec_pre_feeding_, 399.0f, 1e-3);
  CHECK_EQ(std::string(f.psm.getLastEvent().eventID), std::string("EC_FEEDING"));
  CHECK_EQ(ec_processing(f, 400.0f), ControllerState::IDLE);
  CHECK_EQ(ec_processing(f, 650.0f), ControllerState::IDLE);
  CHECK(!has_active_alert(f, "EC_HIGH"));
  CHECK_EQ(ec_processing(f, 651.0f), ControllerState::IDLE);
  CHECK(has_active_alert(f, "EC_HIGH"));
  CHECK_EQ(ec_processing(f, 500.0f), ControllerState::IDLE);  // back in range resolves the alert
  CHECK(!has_active_alert(f, "EC_HIGH"));
}

TEST(ec_processing, multiplier_scales_target_but_not_tolerance) {
  ControllerFixture f;
  f.ctrl.setEcTargetMultiplier(2.0f);  // target 1000, window [900, 1150]
  CHECK_EQ(ec_processing(f, 899.0f), ControllerState::EC_CALCULATING);
  CHECK_EQ(ec_processing(f, 900.0f), ControllerState::IDLE);
  CHECK_EQ(ec_processing(f, 1150.0f), ControllerState::IDLE);
  CHECK(!has_active_alert(f, "EC_HIGH"));
  CHECK_EQ(ec_processing(f, 1151.0f), ControllerState::IDLE);
  CHECK(has_active_alert(f, "EC_HIGH"));
}

TEST(ec_processing, multiplier_setter_clamps_to_0_1_and_2_0) {
  ControllerFixture f;
  f.ctrl.setEcTargetMultiplier(5.0f);
  CHECK_NEAR(f.ctrl.ec_target_multiplier_, 2.0f, 1e-6);
  f.ctrl.setEcTargetMultiplier(0.0f);
  CHECK_NEAR(f.ctrl.ec_target_multiplier_, 0.1f, 1e-6);
}

TEST(ec_processing, skipped_without_sensor_calendar_or_target) {
  ControllerFixture f;
  f.hal.has_ec = false;
  CHECK_EQ(ec_processing(f, 0.0f), ControllerState::IDLE);

  ControllerFixture no_cal(false);
  CHECK_EQ(ec_processing(no_cal, 0.0f), ControllerState::IDLE);

  // ec_target == 0 disables EC feeding for the day (synthetic one-day calendar).
  ControllerFixture g(false);
  CalendarFixture c(R"([{"day": 1, "dose_A_ml_per_L": 1.0}])");
  g.ctrl.setCalendarManager(&c.cal);
  CHECK_EQ(ec_processing(g, 0.0f), ControllerState::IDLE);
}

TEST(ec_processing, twelve_hour_guard_between_feedings) {
  ControllerFixture f;
  f.set_time(2026, 11, 14, 18, 0);
  f.ctrl.last_ec_feeding_timestamp_ = f.hal.timestamp - 43199;
  CHECK_EQ(ec_processing(f, 100.0f), ControllerState::IDLE);
  f.ctrl.last_ec_feeding_timestamp_ = f.hal.timestamp - 43200;
  CHECK_EQ(ec_processing(f, 100.0f), ControllerState::EC_CALCULATING);
  // Without a valid clock the guard is not applied at all.
  f.hal.has_time = false;
  f.ctrl.last_ec_feeding_timestamp_ = f.hal.timestamp - 10;
  CHECK_EQ(ec_processing(f, 100.0f), ControllerState::EC_CALCULATING);
}

TEST(ec_processing, pending_ph_correction_runs_after_a_skipped_ec_check) {
  ControllerFixture f;
  f.ctrl.auto_ph_correction_pending_ = true;
  CHECK_EQ(ec_processing(f, 500.0f), ControllerState::PH_PROCESSING);
  CHECK(!f.ctrl.auto_ph_correction_pending_);
  CHECK_EQ(std::string(f.psm.getLastEvent().eventID), std::string("PH_CORRECTION"));
}

// ---------------------------------------------------------------------------
// handleEcCalculating: total mL/L = dEC / K, split by the calendar A:B:C ratio,
// each pump capped at its calendar dose (mL/L * tank)
// ---------------------------------------------------------------------------

static ControllerState ec_calculating(ControllerFixture &f, float ec) {
  f.force_state(ControllerState::EC_CALCULATING);
  f.hal.ec = ec;
  f.ctrl.handleEcCalculating();
  return f.ctrl.getCurrentState();
}

TEST(ec_dose, production_k_always_hits_the_calendar_cap) {
  // SUSPECT (also described in docs/CONTROL_LOGIC.md, "the unit of EC_K_feed"): the dose is
  // computed as delta_ec / K while K is learned as (mL/L) / delta_ec. With K inside its clamp
  // range [0.00002, 0.0005] the quotient is always thousands of mL/L, so every automatic
  // installment equals the cap = full calendar dose, whatever the EC gap or K is.
  ControllerFixture f;  // day 1: A=B=C=0.8 mL/L, target 500 ; tank 27.4 L
  for (float k : {0.00002f, 0.00015f, 0.0005f}) {
    for (float ec : {0.0f, 300.0f, 480.0f}) {
      f.ctrl.ec_K_feed_ = k;
      f.ctrl.ec_total_ml_per_L_ = 0.0f;
      CHECK_EQ(ec_calculating(f, ec), ControllerState::EC_FEEDING);
      CHECK_NEAR(f.ctrl.ec_dose_A_ml_, 21.92f, 5e-3);  // 0.8 mL/L * 27.4 L
      CHECK_NEAR(f.ctrl.ec_dose_B_ml_, 21.92f, 5e-3);
      CHECK_NEAR(f.ctrl.ec_dose_C_ml_, 21.92f, 5e-3);
      CHECK_NEAR(f.ctrl.ec_total_ml_per_L_, 2.4f, 1e-3);  // the capped amount is what gets accounted
    }
  }
}

// The tests below pin the uncapped branch of the formula. It needs K >= ~100, which the
// clamp makes unreachable in production; K is forced here to exercise the arithmetic.
static const float K_UNCAPPED = 100.0f;

TEST(ec_dose, uncapped_formula_equal_ratio_day_1) {
  ControllerFixture f;
  f.ctrl.ec_K_feed_ = K_UNCAPPED;
  CHECK_EQ(ec_calculating(f, 300.0f), ControllerState::EC_FEEDING);
  // dEC 200 / 100 = 2.0 mL/L ; each pump 1/3 * 2.0 * 27.4 = 18.267 mL (cap 21.92)
  CHECK_NEAR(f.ctrl.ec_dose_A_ml_, 18.267f, 5e-3);
  CHECK_NEAR(f.ctrl.ec_dose_B_ml_, 18.267f, 5e-3);
  CHECK_NEAR(f.ctrl.ec_dose_C_ml_, 18.267f, 5e-3);
  CHECK_NEAR(f.ctrl.ec_total_ml_per_L_, 2.0f, 1e-3);
  // accumulates over retries within one cycle
  CHECK_EQ(ec_calculating(f, 400.0f), ControllerState::EC_FEEDING);  // 1.0 mL/L more
  CHECK_NEAR(f.ctrl.ec_total_ml_per_L_, 3.0f, 1e-3);
}

TEST(ec_dose, uncapped_formula_ratio_split_day_10) {
  ControllerFixture f;
  f.cal.set_current_day(10);  // A 1.44, B 0.96, C 0.48 mL/L ; target 800
  f.ctrl.ec_K_feed_ = K_UNCAPPED;
  CHECK_EQ(ec_calculating(f, 600.0f), ControllerState::EC_FEEDING);
  // 2.0 mL/L * 27.4 L = 54.8 mL split 3:2:1
  CHECK_NEAR(f.ctrl.ec_dose_A_ml_, 27.4f, 5e-3);
  CHECK_NEAR(f.ctrl.ec_dose_B_ml_, 18.267f, 5e-3);
  CHECK_NEAR(f.ctrl.ec_dose_C_ml_, 9.133f, 5e-3);
}

TEST(ec_dose, cap_is_applied_per_pump) {
  ControllerFixture f;
  f.cal.set_current_day(10);
  f.ctrl.ec_K_feed_ = K_UNCAPPED;
  CHECK_EQ(ec_calculating(f, 0.0f), ControllerState::EC_FEEDING);
  // dEC 800 -> 8 mL/L -> A 109.6 / B 73.07 / C 36.53 mL, capped at mL/L * 27.4
  CHECK_NEAR(f.ctrl.ec_dose_A_ml_, 39.456f, 5e-3);
  CHECK_NEAR(f.ctrl.ec_dose_B_ml_, 26.304f, 5e-3);
  CHECK_NEAR(f.ctrl.ec_dose_C_ml_, 13.152f, 5e-3);
  CHECK_NEAR(f.ctrl.ec_total_ml_per_L_, 2.88f, 1e-3);
}

TEST(ec_dose, no_dose_within_10_uS_of_target) {
  ControllerFixture f;
  CHECK_EQ(ec_calculating(f, 490.0f), ControllerState::IDLE);  // delta 10 -> nothing
  CHECK_EQ(f.ctrl.ec_dose_A_ml_, 0.0f);
  CHECK(!f.psm.hasEvent());
  CHECK_EQ(ec_calculating(f, 489.0f), ControllerState::EC_FEEDING);
  CHECK(f.ctrl.ec_dose_A_ml_ > 0.0f);
}

TEST(ec_dose, multiplier_scales_target) {
  ControllerFixture f;
  f.ctrl.ec_K_feed_ = K_UNCAPPED;
  f.ctrl.setEcTargetMultiplier(1.2f);  // target 600
  CHECK_EQ(ec_calculating(f, 450.0f), ControllerState::EC_FEEDING);
  // dEC 150 / 100 = 1.5 mL/L -> 41.1 mL total -> 13.7 each
  CHECK_NEAR(f.ctrl.ec_dose_A_ml_, 13.7f, 5e-3);
}

TEST(ec_dose, tank_volume_fallback_10_liters) {
  ControllerFixture f;
  f.ctrl.ec_K_feed_ = K_UNCAPPED;
  f.hal.tank_volume_delta = 0.0f;
  CHECK_EQ(ec_calculating(f, 300.0f), ControllerState::EC_FEEDING);
  CHECK_NEAR(f.ctrl.ec_dose_A_ml_, 6.667f, 5e-3);  // 1/3 * 2.0 * 10
}

TEST(ec_dose, uses_pre_feeding_value_when_sensor_has_no_reading) {
  ControllerFixture f;
  f.ctrl.ec_K_feed_ = K_UNCAPPED;
  f.hal.has_ec = false;
  f.ctrl.ec_pre_feeding_ = 300.0f;
  CHECK_EQ(ec_calculating(f, 9999.0f), ControllerState::EC_FEEDING);
  CHECK_NEAR(f.ctrl.ec_dose_A_ml_, 18.267f, 5e-3);
}

TEST(ec_dose, day_without_nutrient_ratios_is_skipped) {
  ControllerFixture f;
  f.cal.set_current_day(120);  // flush: all doses 0, EC target 150
  CHECK_EQ(ec_calculating(f, 0.0f), ControllerState::IDLE);
  CHECK(!f.psm.hasEvent());
}

TEST(ec_dose, missing_calendar_aborts) {
  ControllerFixture f(false);
  CHECK_EQ(ec_calculating(f, 0.0f), ControllerState::IDLE);
}

// ---------------------------------------------------------------------------
// handleEcFeeding: dose -> pump seconds via HAL flow rates, A -> B -> C
// ---------------------------------------------------------------------------

TEST(ec_feeding, pump_on_times_follow_flow_rates) {
  ControllerFixture f;
  f.ctrl.ec_K_feed_ = 0.00015f;
  ec_calculating(f, 300.0f);  // 21.92 mL per pump (capped, see ec_dose tests)
  run_ec_feeding(f);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::EC_MIXING);
  // on-time = dose / flow rate + 200 ms margin (10 ms tick)
  CHECK_NEAR(on_time_ms(f.hal, "NutrientPumpA"), 21920 / 1.75 + 200, 30);  // 1.75 mL/s -> 12.53 s
  CHECK_NEAR(on_time_ms(f.hal, "NutrientPumpB"), 21920 / 0.75 + 200, 30);  // 0.75 mL/s -> 29.23 s
  CHECK_NEAR(on_time_ms(f.hal, "NutrientPumpC"), 21920 / 0.75 + 200, 30);
  // strictly sequential: A on, A off, B on, B off, C on, C off
  REQUIRE(f.hal.calls.size() == 6);
  const char *order[] = {"NutrientPumpA", "NutrientPumpA", "NutrientPumpB",
                         "NutrientPumpB", "NutrientPumpC", "NutrientPumpC"};
  for (int i = 0; i < 6; i++) {
    CHECK_EQ(f.hal.calls[i].id, std::string(order[i]));
    CHECK_EQ(f.hal.calls[i].state, i % 2 == 0);
  }
}

TEST(ec_feeding, automatic_cycle_counts_the_dose_twice_for_k_learning) {
  // SUSPECT: handleEcCalculating adds the dose to ec_total_ml_per_L_ (controller.cpp ~2333) and
  // handleEcFeeding adds the same dose again when the last pump finishes (~2370). In the
  // automatic path (EC_CALCULATING -> EC_FEEDING) the K-factor update therefore sees 2x the
  // real mL/L. The manual path (FEEDING -> EC_FEEDING) counts once.
  ControllerFixture f;
  f.ctrl.ec_K_feed_ = 0.00015f;
  ec_calculating(f, 300.0f);
  CHECK_NEAR(f.ctrl.ec_total_ml_per_L_, 2.4f, 1e-3);  // real dose: 3 x 21.92 mL / 27.4 L
  run_ec_feeding(f);
  CHECK_NEAR(f.ctrl.ec_total_ml_per_L_, 4.8f, 1e-3);  // what updateEcKFactor will get
}

TEST(ec_feeding, zero_dose_pumps_are_skipped) {
  ControllerFixture f;
  f.force_state(ControllerState::EC_FEEDING);
  f.ctrl.state_counter_ = 0;
  f.ctrl.ec_dose_A_ml_ = 0.0f;
  f.ctrl.ec_dose_B_ml_ = 3.0f;
  f.ctrl.ec_dose_C_ml_ = 0.0f;
  run_ec_feeding(f);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::EC_MIXING);
  CHECK_EQ(f.hal.count("pump", "NutrientPumpA", true), 0);
  CHECK_EQ(f.hal.count("pump", "NutrientPumpB", true), 1);
  CHECK_EQ(f.hal.count("pump", "NutrientPumpC", true), 0);
}

// ---------------------------------------------------------------------------
// handleFeeding (manual "Feed"): calendar dose in installments of 1/5
// ---------------------------------------------------------------------------

TEST(manual_feeding, installments_are_one_fifth_of_calendar_dose) {
  ControllerFixture f;
  f.cal.set_current_day(10);  // 1.44 / 0.96 / 0.48 mL/L
  f.hal.ec = 321.0f;
  f.force_state(ControllerState::FEEDING);
  f.ctrl.handleFeeding();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::EC_FEEDING);
  CHECK(f.ctrl.feeding_manual_mode_);
  CHECK_NEAR(f.ctrl.ec_dose_A_ml_, 1.44f * 27.4f / 5.0f, 1e-3);  // 7.891
  CHECK_NEAR(f.ctrl.ec_dose_B_ml_, 0.96f * 27.4f / 5.0f, 1e-3);  // 5.261
  CHECK_NEAR(f.ctrl.ec_dose_C_ml_, 0.48f * 27.4f / 5.0f, 1e-3);  // 2.630
  CHECK_NEAR(f.ctrl.ec_pre_feeding_, 321.0f, 1e-3);
}

TEST(manual_feeding, reservoir_change_uses_total_volume) {
  ControllerFixture f;
  f.cal.set_current_day(10);
  f.ctrl.is_reservoir_change_ = true;
  f.force_state(ControllerState::FEEDING);
  f.ctrl.handleFeeding();
  CHECK_NEAR(f.ctrl.ec_dose_A_ml_, 1.44f * 47.5f / 5.0f, 1e-3);  // 13.68
}

TEST(manual_feeding, fixed_full_dose_when_day_has_no_ec_target) {
  ControllerFixture f(false);
  CalendarFixture c(R"([{"day": 1, "dose_A_ml_per_L": 1.0, "dose_B_ml_per_L": 0.5, "dose_C_ml_per_L": 0.25}])");
  f.ctrl.setCalendarManager(&c.cal);
  f.force_state(ControllerState::FEEDING);
  f.ctrl.handleFeeding();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::EC_FEEDING);
  CHECK(!f.ctrl.feeding_manual_mode_);
  CHECK_NEAR(f.ctrl.ec_dose_A_ml_, 27.4f, 1e-3);
  CHECK_NEAR(f.ctrl.ec_dose_B_ml_, 13.7f, 1e-3);
  CHECK_NEAR(f.ctrl.ec_dose_C_ml_, 6.85f, 1e-3);
}

TEST(mother_mix, fixed_recipe_per_liter) {
  ControllerFixture f;
  f.force_state(ControllerState::IDLE);
  f.ctrl.startMotherNutrients(10.0f);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::EC_FEEDING);
  CHECK_NEAR(f.ctrl.ec_dose_A_ml_, 14.4f, 1e-3);
  CHECK_NEAR(f.ctrl.ec_dose_B_ml_, 9.6f, 1e-3);
  CHECK_NEAR(f.ctrl.ec_dose_C_ml_, 4.8f, 1e-3);
  CHECK(f.ctrl.is_mother_mix_);
}

TEST(mother_mix, rejects_invalid_volume_or_busy_state) {
  ControllerFixture f;
  f.force_state(ControllerState::IDLE);
  f.ctrl.startMotherNutrients(0.0f);
  f.ctrl.startMotherNutrients(15.1f);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  f.ctrl.startMotherNutrients(15.0f);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::EC_FEEDING);

  ControllerFixture busy;
  busy.force_state(ControllerState::PH_MIXING);
  busy.ctrl.startMotherNutrients(5.0f);
  CHECK_EQ(busy.ctrl.getCurrentState(), ControllerState::PH_MIXING);
  CHECK(!busy.ctrl.is_mother_mix_);
}

// ---------------------------------------------------------------------------
// handleEcMeasuring: 30 s settle, K update, retry / finish decision
// ---------------------------------------------------------------------------

static ControllerState ec_measuring(ControllerFixture &f, float ec_after) {
  f.force_state(ControllerState::EC_MEASURING);
  f.ctrl.handleEcMeasuring();  // entry: counts the attempt
  host::advance_millis(29999);
  f.hal.ec = ec_after;
  f.ctrl.handleEcMeasuring();  // still settling
  if (f.ctrl.getCurrentState() != ControllerState::EC_MEASURING) return f.ctrl.getCurrentState();
  host::advance_millis(1);
  f.ctrl.handleEcMeasuring();
  return f.ctrl.getCurrentState();
}

TEST(ec_measuring, in_range_finishes_with_ph_check_and_learns_k) {
  ControllerFixture f;  // day 1: min 400
  f.ctrl.ec_K_feed_ = 0.00015f;
  f.ctrl.ec_pre_feeding_ = 300.0f;
  f.ctrl.ec_total_ml_per_L_ = 0.04f;
  CHECK_EQ(ec_measuring(f, 500.0f), ControllerState::PH_PROCESSING);
  CHECK_EQ(f.ctrl.ec_attempt_count_, 1);
  CHECK(f.ctrl.ph_post_feed_stabilize_);
  CHECK_NEAR(f.ctrl.ec_K_feed_, 0.00016f, 1e-8);  // 0.04 / 200 = 0.0002 -> EMA
  CHECK_EQ(std::string(f.psm.getLastEvent().eventID), std::string("PH_CORRECTION"));
}

TEST(ec_measuring, still_low_retries_up_to_three_times_then_alerts) {
  ControllerFixture f;
  f.ctrl.ec_pre_feeding_ = 300.0f;
  CHECK_EQ(ec_measuring(f, 350.0f), ControllerState::EC_CALCULATING);
  CHECK_EQ(ec_measuring(f, 360.0f), ControllerState::EC_CALCULATING);
  CHECK(!has_active_alert(f, "EC_LOW"));
  CHECK_EQ(ec_measuring(f, 370.0f), ControllerState::PH_PROCESSING);
  CHECK_EQ(f.ctrl.ec_attempt_count_, 3);
  CHECK(has_active_alert(f, "EC_LOW"));
}

TEST(ec_measuring, manual_mode_repeats_same_installment_up_to_20) {
  ControllerFixture f;
  f.ctrl.feeding_manual_mode_ = true;
  f.ctrl.ec_pre_feeding_ = 300.0f;
  for (int i = 1; i <= 19; i++) CHECK_EQ(ec_measuring(f, 350.0f), ControllerState::EC_FEEDING);
  CHECK_EQ(ec_measuring(f, 350.0f), ControllerState::PH_PROCESSING);
  CHECK_EQ(f.ctrl.ec_attempt_count_, 20);
  CHECK(!f.ctrl.feeding_manual_mode_);
  CHECK(has_active_alert(f, "EC_LOW"));
}

TEST(ec_measuring, no_reading_aborts_to_idle) {
  ControllerFixture f;
  f.hal.has_ec = false;
  CHECK_EQ(ec_measuring(f, 0.0f), ControllerState::IDLE);
}
