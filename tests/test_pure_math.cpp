// Pure free functions extracted from PlantOSController (plan step 2.4).
// Real code under test: components/plantos_controller/dosing_math.cpp, schedule_math.cpp
// Called directly, without a controller fixture. Expected values are worked out by hand.
#include "minitest.h"
#include "esphome/components/plantos_controller/dosing_math.h"
#include "esphome/components/plantos_controller/schedule_math.h"

#include <cmath>
#include <vector>

using namespace plantos_controller;

static constexpr int64_t DAY = 86400;

// ---------------------------------------------------------------------------
// schedule_math: grow-light window
// ---------------------------------------------------------------------------

TEST(light_window, same_day_08_to_16) {
  using schedule_math::lightShouldBeOn;
  CHECK(!lightShouldBeOn(0, 480, 960));
  CHECK(!lightShouldBeOn(479, 480, 960));
  CHECK(lightShouldBeOn(480, 480, 960));    // ON edge is inclusive
  CHECK(lightShouldBeOn(959, 480, 960));
  CHECK(!lightShouldBeOn(960, 480, 960));   // OFF edge is exclusive
  CHECK(!lightShouldBeOn(1439, 480, 960));
}

TEST(light_window, wraps_past_midnight_16_to_08) {
  using schedule_math::lightShouldBeOn;
  CHECK(!lightShouldBeOn(959, 960, 480));
  CHECK(lightShouldBeOn(960, 960, 480));
  CHECK(lightShouldBeOn(1439, 960, 480));   // 23:59
  CHECK(lightShouldBeOn(0, 960, 480));      // 00:00
  CHECK(lightShouldBeOn(479, 960, 480));    // 07:59
  CHECK(!lightShouldBeOn(480, 960, 480));   // 08:00
  CHECK(!lightShouldBeOn(720, 960, 480));
}

TEST(light_window, on_equals_off_is_always_on) {
  // on == off takes the wrap branch: now >= on || now < on
  CHECK(schedule_math::lightShouldBeOn(0, 600, 600));
  CHECK(schedule_math::lightShouldBeOn(600, 600, 600));
  CHECK(schedule_math::lightShouldBeOn(1439, 600, 600));
}

TEST(light_window, hour_night_window) {
  using schedule_math::hourInNightWindow;
  CHECK(hourInNightWindow(22, 22, 9));
  CHECK(hourInNightWindow(0, 22, 9));
  CHECK(hourInNightWindow(8, 22, 9));
  CHECK(!hourInNightWindow(9, 22, 9));
  CHECK(!hourInNightWindow(21, 22, 9));
  CHECK(!hourInNightWindow(0, 1, 6));
  CHECK(hourInNightWindow(1, 1, 6));
  CHECK(hourInNightWindow(5, 1, 6));
  CHECK(!hourInNightWindow(6, 1, 6));
  CHECK(!hourInNightWindow(5, 5, 5));       // start == end: never night
}

// ---------------------------------------------------------------------------
// schedule_math: dates, grow day, week
// ---------------------------------------------------------------------------

TEST(schedule_math, days_from_civil) {
  using schedule_math::daysFromCivil;
  CHECK_EQ(daysFromCivil(1970, 1, 1), 0);
  CHECK_EQ(daysFromCivil(1969, 12, 31), -1);
  CHECK_EQ(daysFromCivil(2000, 3, 1), 11017);    // 30*365 + 7 leap days + 31 + 29
  CHECK_EQ(daysFromCivil(2024, 2, 29), 19782);   // 54*365 + 13 + 31 + 28
  CHECK_EQ(daysFromCivil(2024, 3, 1), 19783);
  CHECK_EQ(daysFromCivil(2026, 11, 14), 20771);  // 56*365 + 14 + 304 + 13
}

TEST(schedule_math, grow_days_elapsed_truncates) {
  using schedule_math::growDaysElapsed;
  CHECK_EQ(growDaysElapsed(DAY - 1, 0), 0);
  CHECK_EQ(growDaysElapsed(DAY, 0), 1);
  CHECK_EQ(growDaysElapsed(-1, 0), 0);           // less than a day before start -> 0, not -1
  CHECK_EQ(growDaysElapsed(-DAY, 0), -1);
  CHECK_EQ(growDaysElapsed(1000 * DAY + 5, 990 * DAY), 10);
}

TEST(schedule_math, grow_day_wraps_after_120) {
  using schedule_math::growDayFromElapsed;
  CHECK_EQ((int)growDayFromElapsed(0), 1);
  CHECK_EQ((int)growDayFromElapsed(119), 120);
  CHECK_EQ((int)growDayFromElapsed(120), 1);
  CHECK_EQ((int)growDayFromElapsed(239), 120);
}

TEST(schedule_math, week_number_and_weekday_are_utc) {
  CHECK_EQ(schedule_math::weekNumber(0), 0);
  CHECK_EQ(schedule_math::weekNumber(7 * DAY - 1), 0);
  CHECK_EQ(schedule_math::weekNumber(7 * DAY), 1);  // Thursday 1970-01-08 00:00 UTC
  CHECK_EQ((int)schedule_math::dayOfWeek(0), 4);        // 1970-01-01 Thursday
  CHECK_EQ((int)schedule_math::dayOfWeek(3 * DAY), 0);  // 1970-01-04 Sunday
  CHECK_EQ((int)schedule_math::dayOfWeek(3 * DAY - 1), 6);
}

// ---------------------------------------------------------------------------
// schedule_math: pH check slots
// ---------------------------------------------------------------------------

TEST(ph_check_due, two_hour_slots) {
  const int64_t day_start = 1000 * DAY;
  const uint32_t ssm = 5 * 3600 + 10 * 60;  // 05:10 -> slot 2 (04:00)
  auto r = schedule_math::phCheckDue(day_start + ssm, day_start + 4 * 3600 - 1, ssm, 7200);
  CHECK(r.due);
  CHECK(!r.multi_day);
  CHECK_EQ(r.slot, 2u);
  CHECK_EQ(r.slots_per_day, 12u);
  CHECK_EQ(r.slot_hour, 4u);
  CHECK_EQ(r.slot_minute, 0u);
  r = schedule_math::phCheckDue(day_start + ssm, day_start + 4 * 3600, ssm, 7200);
  CHECK(!r.due);  // already checked at the slot start
}

TEST(ph_check_due, ninety_minute_slots_report_minutes) {
  const int64_t day_start = 1000 * DAY;
  const uint32_t ssm = 5 * 3600 + 10 * 60;  // 18600 / 5400 = slot 3 -> 04:30
  auto r = schedule_math::phCheckDue(day_start + ssm, 0, ssm, 5400);
  CHECK(r.due);
  CHECK_EQ(r.slot, 3u);
  CHECK_EQ(r.slot_hour, 4u);
  CHECK_EQ(r.slot_minute, 30u);
  CHECK_EQ(r.slots_per_day, 16u);
}

TEST(ph_check_due, multi_day_waits_a_minute_after_midnight) {
  const int64_t last = 1000 * DAY + 50000;
  auto r = schedule_math::phCheckDue(1002 * DAY + 30, last, 30, 2 * DAY);
  CHECK(r.multi_day);
  CHECK_EQ(r.days_since_last, 2);
  CHECK_EQ(r.interval_days, 2);
  CHECK(!r.due);  // 00:00:30 < 60 s
  r = schedule_math::phCheckDue(1002 * DAY + 60, last, 60, 2 * DAY);
  CHECK(r.due);
  r = schedule_math::phCheckDue(1001 * DAY + 80000, last, 80000, 2 * DAY);
  CHECK(!r.due);  // only one epoch day since last
}

// ---------------------------------------------------------------------------
// dosing_math: pH
// ---------------------------------------------------------------------------

TEST(dosing_math, acid_dose_and_clamp) {
  CHECK_NEAR(dosing_math::acidDoseMl(0.85f, 20.0f, 0.1f, 0.5f, 10.0f), 1.7f, 1e-5);  // 0.85*20*0.1
  CHECK_NEAR(dosing_math::acidDoseMl(0.1f, 1.0f, 0.1f, 0.5f, 10.0f), 0.5f, 1e-6);    // 0.01 -> min
  CHECK_NEAR(dosing_math::acidDoseMl(5.0f, 50.0f, 0.1f, 0.5f, 10.0f), 10.0f, 1e-6);  // 25 -> max
}

TEST(dosing_math, ph_k_update_clamps_observed_only) {
  auto k = dosing_math::phKUpdate(2.0f, 0.5f, 20.0f, 0.1f, 0.2f, 0.01f, 0.5f);
  CHECK_NEAR(k.observed, 0.2f, 1e-6);   // 2 / (0.5*20)
  CHECK_NEAR(k.updated, 0.12f, 1e-6);   // 0.2*0.2 + 0.8*0.1
  k = dosing_math::phKUpdate(2.0f, 0.5f, 20.0f, 0.1f, 0.2f, 0.01f, 0.15f);
  CHECK_NEAR(k.observed, 0.15f, 1e-6);
  CHECK_NEAR(k.updated, 0.11f, 1e-6);   // 0.2*0.15 + 0.8*0.1
  k = dosing_math::phKUpdate(2.0f, 0.5f, 20.0f, 1.0f, 0.2f, 0.01f, 0.5f);
  CHECK_NEAR(k.updated, 0.84f, 1e-6);   // 0.2*0.2 + 0.8*1.0, above k_max and NOT clamped
}

TEST(dosing_math, ph_mixing_duration_raw) {
  CHECK_NEAR(dosing_math::phMixingDurationRawMs(1.7f), 29999.8f, 1.0);    // 8344.07*1.7 + 15814.9
  CHECK_NEAR(dosing_math::phMixingDurationRawMs(70.0f), 599899.8f, 1.0);
  CHECK_NEAR(dosing_math::phMixingDurationRawMs(27.4f), 244442.4f, 1.0);
  CHECK_NEAR(dosing_math::phMixingDurationRawMs(0.0f), 15814.9f, 0.1);  // below 30 s, caller clamps
}

TEST(dosing_math, robust_average) {
  CHECK_EQ(dosing_math::robustAverage({}), 0.0f);
  CHECK_EQ(dosing_math::robustAverage({7.0f}), 7.0f);
  CHECK_NEAR(dosing_math::robustAverage({1.0f, 2.0f}), 1.5f, 1e-6);         // 2 readings: nothing dropped
  CHECK_NEAR(dosing_math::robustAverage({100.0f, 1.0f, 2.0f}), 2.0f, 1e-6); // min and max dropped
  CHECK_NEAR(dosing_math::robustAverage({1, 2, 3, 4, 5, 6, 7, 8, 9, 100}), 5.5f, 1e-6);  // 2..9
}

TEST(dosing_math, stability_helpers) {
  CHECK(!dosing_math::lastThreeWithin({6.0f, 6.05f}, 0.1f));
  CHECK(dosing_math::lastThreeWithin({9.0f, 6.0f, 6.05f, 6.08f}, 0.1f));  // only the last three count
  CHECK(!dosing_math::lastThreeWithin({6.0f, 6.2f, 6.1f}, 0.1f));
  CHECK_NEAR(dosing_math::maxPairwiseDiff(1.0f, 3.0f, 2.0f), 2.0f, 1e-6);
  CHECK_NEAR(dosing_math::maxPairwiseDiff(5.0f, 5.0f, 5.0f), 0.0f, 1e-6);
}

// ---------------------------------------------------------------------------
// dosing_math: EC
// ---------------------------------------------------------------------------

TEST(dosing_math, ec_window_and_classify) {
  auto w = dosing_math::ecWindow(1000.0f, 100.0f);
  CHECK_NEAR(w.min, 900.0f, 1e-3);
  CHECK_NEAR(w.max, 1150.0f, 1e-3);  // +1.5 * tolerance
  using dosing_math::EcLevel;
  CHECK(dosing_math::classifyEc(899.0f, 1000.0f, 100.0f) == EcLevel::BELOW_RANGE);
  CHECK(dosing_math::classifyEc(900.0f, 1000.0f, 100.0f) == EcLevel::IN_RANGE);
  CHECK(dosing_math::classifyEc(1150.0f, 1000.0f, 100.0f) == EcLevel::IN_RANGE);
  CHECK(dosing_math::classifyEc(1151.0f, 1000.0f, 100.0f) == EcLevel::ABOVE_RANGE);
  CHECK(dosing_math::classifyEc(NAN, 1000.0f, 100.0f) == EcLevel::BELOW_RANGE);
}

TEST(dosing_math, ec_doses_split_by_ratio) {
  // SUSPECT (kept): total = delta_ec / k = 400 / 200 = 2 mL/L
  auto d = dosing_math::calcEcDoses(400.0f, 200.0f, 20.0f, 1.0f, 1.0f, 2.0f);
  CHECK(d.valid);
  CHECK_NEAR(d.total_ml_per_L, 2.0f, 1e-6);
  CHECK_NEAR(d.a_ml, 10.0f, 1e-5);   // 1/4 * 2 * 20
  CHECK_NEAR(d.b_ml, 10.0f, 1e-5);
  CHECK_NEAR(d.c_ml, 20.0f, 1e-5);   // 2/4 * 2 * 20
  CHECK_NEAR(d.dosed_ml_per_L, 2.0f, 1e-6);  // 40 mL / 20 L
}

TEST(dosing_math, ec_doses_capped_at_calendar_dose) {
  // total = 400 / 50 = 8 mL/L; uncapped A = 1/4*8*20 = 40 > cap 1*20
  auto d = dosing_math::calcEcDoses(400.0f, 50.0f, 20.0f, 1.0f, 1.0f, 2.0f);
  CHECK_NEAR(d.a_ml, 20.0f, 1e-5);
  CHECK_NEAR(d.b_ml, 20.0f, 1e-5);
  CHECK_NEAR(d.c_ml, 40.0f, 1e-5);   // uncapped 80, cap 2*20
  CHECK_NEAR(d.dosed_ml_per_L, 4.0f, 1e-6);
}

TEST(dosing_math, ec_doses_invalid_ratio_and_zero_tank) {
  auto d = dosing_math::calcEcDoses(400.0f, 200.0f, 20.0f, 0.0f, 0.0f, 0.0f);
  CHECK(!d.valid);
  CHECK_NEAR(d.total_ml_per_L, 2.0f, 1e-6);
  CHECK_EQ(d.a_ml + d.b_ml + d.c_ml, 0.0f);
  d = dosing_math::calcEcDoses(400.0f, 200.0f, 0.0f, 1.0f, 1.0f, 2.0f);
  CHECK(d.valid);
  CHECK_EQ(d.dosed_ml_per_L, 0.0f);
}

TEST(dosing_math, calendar_and_installment_doses) {
  auto full = dosing_math::calendarDoses(1.0f, 2.0f, 3.0f, 10.0f);
  CHECK_NEAR(full.a_ml, 10.0f, 1e-6);
  CHECK_NEAR(full.b_ml, 20.0f, 1e-6);
  CHECK_NEAR(full.c_ml, 30.0f, 1e-6);
  auto part = dosing_math::installmentDoses(full);
  CHECK_NEAR(part.a_ml, 2.0f, 1e-6);
  CHECK_NEAR(part.b_ml, 4.0f, 1e-6);
  CHECK_NEAR(part.c_ml, 6.0f, 1e-6);
}

TEST(dosing_math, ec_k_update_clamps_twice) {
  auto k = dosing_math::ecKUpdate(2.0f, 400.0f, 0.004f, 0.2f, 0.001f, 0.02f);
  CHECK_NEAR(k.observed, 0.005f, 1e-7);   // 2 / 400
  CHECK_NEAR(k.updated, 0.0042f, 1e-7);   // 0.2*0.005 + 0.8*0.004
  k = dosing_math::ecKUpdate(2.0f, 400.0f, 1.0f, 0.2f, 0.001f, 0.02f);
  CHECK_NEAR(k.updated, 0.02f, 1e-7);     // 0.801 clamped to k_max
}

TEST(dosing_math, reading_stats) {
  auto s = dosing_math::readingStats({2, 4, 4, 4, 5, 5, 7, 9});
  CHECK_NEAR(s.mean, 5.0f, 1e-6);
  CHECK_NEAR(s.std_dev, 2.0f, 1e-6);  // population std dev
  CHECK_NEAR(s.cv, 0.4f, 1e-6);
}
