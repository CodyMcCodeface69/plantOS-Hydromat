// CalendarManager: JSON parse of the production schedule, per-day lookup, unit
// conversion, day counter + NVS persistence, real-date sync.
// Real code under test: components/calendar_manager/CalendarManager.{h,cpp}
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include "minitest.h"
#include "fixture.h"

using esphome::calendar_manager::CalendarManager;
using esphome::calendar_manager::DailySchedule;

// ---------------------------------------------------------------------------
// Production schedule (extracted from plantOS.yaml at build time)
// ---------------------------------------------------------------------------

static std::string golden_row(const DailySchedule &s) {
  char buf[200];
  snprintf(buf, sizeof(buf), "%d,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%d,%.2f,%.2f", s.day_number, s.target_ph_min,
           s.target_ph_max, s.nutrient_A_ml_per_liter, s.nutrient_B_ml_per_liter, s.nutrient_C_ml_per_liter,
           s.light_on_time, s.light_off_time, s.ec_target, s.ec_tolerance);
  return buf;
}
static const char *GOLDEN_HEADER =
    "day,ph_min,ph_max,dose_A_ml_per_L,dose_B_ml_per_L,dose_C_ml_per_L,light_on_min,light_off_min,"
    "ec_target_uS_cm,ec_tolerance_uS_cm";

// The upcoming phase-table rewrite must reproduce exactly these 120 rows.
// Regenerate with `make golden` only when the schedule is changed on purpose.
TEST(calendar, golden_haupt_schedule_all_120_days) {
  CalendarFixture f;
  std::vector<std::string> rows;
  for (int day = 1; day <= 120; day++) rows.push_back(golden_row(f.cal.get_schedule(day)));

  const std::string path = std::string(PLANTOS_GOLDEN_DIR) + "/haupt_schedule.csv";
  if (getenv("PLANTOS_WRITE_GOLDEN")) {
    std::ofstream out(path);
    out << GOLDEN_HEADER << "\n";
    for (auto &r : rows) out << r << "\n";
    printf("wrote %s\n", path.c_str());
    return;
  }

  std::ifstream in(path);
  REQUIRE(in.good());
  std::string line;
  REQUIRE(static_cast<bool>(std::getline(in, line)));
  CHECK_EQ(line, std::string(GOLDEN_HEADER));
  size_t n = 0;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    REQUIRE(n < rows.size());
    CHECK_EQ(rows[n], line);
    n++;
  }
  CHECK_EQ(n, 120u);
}

TEST(calendar, production_schedule_parses_120_days) {
  CalendarFixture f;
  CHECK_EQ(f.cal.schedule_map_.size(), 120u);
  for (int day = 1; day <= 120; day++) {
    CHECK(f.cal.schedule_map_.count(day) == 1);
    CHECK_EQ(f.cal.get_schedule(day).day_number, day);
  }
}

TEST(calendar, ec_is_converted_from_mS_to_uS) {
  CalendarFixture f;
  // JSON day 1: "ec_target": 0.50, "ec_tolerance": 0.10 (mS/cm)
  CHECK_NEAR(f.cal.get_schedule(1).ec_target, 500.0f, 0.01);
  CHECK_NEAR(f.cal.get_schedule(1).ec_tolerance, 100.0f, 0.01);
  // JSON day 11: 1.20 / 0.20
  CHECK_NEAR(f.cal.get_schedule(11).ec_target, 1200.0f, 0.01);
  CHECK_NEAR(f.cal.get_schedule(11).ec_tolerance, 200.0f, 0.01);
  // JSON day 60: 2.2 / 0.2
  CHECK_NEAR(f.cal.get_schedule(60).ec_target, 2200.0f, 0.01);
  // JSON day 120 (flush): 0.15 / 0.15
  CHECK_NEAR(f.cal.get_schedule(120).ec_target, 150.0f, 0.01);
  CHECK_NEAR(f.cal.get_schedule(120).ec_tolerance, 150.0f, 0.01);
}

TEST(calendar, spot_values_of_production_days) {
  CalendarFixture f;
  DailySchedule d1 = f.cal.get_schedule(1);
  CHECK_NEAR(d1.target_ph_min, 5.8f, 1e-5);
  CHECK_NEAR(d1.target_ph_max, 6.2f, 1e-5);
  CHECK_NEAR(d1.nutrient_A_ml_per_liter, 0.8f, 1e-5);
  CHECK_NEAR(d1.nutrient_B_ml_per_liter, 0.8f, 1e-5);
  CHECK_NEAR(d1.nutrient_C_ml_per_liter, 0.8f, 1e-5);
  CHECK_EQ(d1.light_on_time, 960);   // 16:00
  CHECK_EQ(d1.light_off_time, 480);  // 08:00

  DailySchedule d8 = f.cal.get_schedule(8);
  CHECK_NEAR(d8.nutrient_A_ml_per_liter, 1.01f, 1e-5);
  CHECK_NEAR(d8.nutrient_B_ml_per_liter, 0.85f, 1e-5);
  CHECK_NEAR(d8.nutrient_C_ml_per_liter, 0.69f, 1e-5);

  DailySchedule d60 = f.cal.get_schedule(60);
  CHECK_NEAR(d60.target_ph_min, 6.0f, 1e-5);
  CHECK_NEAR(d60.target_ph_max, 6.4f, 1e-5);
  CHECK_NEAR(d60.nutrient_C_ml_per_liter, 1.92f, 1e-5);
  CHECK_EQ(d60.light_off_time, 240);  // 04:00

  DailySchedule d120 = f.cal.get_schedule(120);
  CHECK_NEAR(d120.target_ph_min, 5.5f, 1e-5);
  CHECK_NEAR(d120.target_ph_max, 6.5f, 1e-5);
  CHECK_EQ(d120.nutrient_A_ml_per_liter, 0.0f);
  CHECK_EQ(d120.nutrient_B_ml_per_liter, 0.0f);
  CHECK_EQ(d120.nutrient_C_ml_per_liter, 0.0f);
}

// ---------------------------------------------------------------------------
// Lookup outside the table
// ---------------------------------------------------------------------------

TEST(calendar, out_of_range_days_return_defaults) {
  CalendarFixture f;
  for (int day : {0, 121, 200, 255}) {
    DailySchedule s = f.cal.get_schedule(day);
    CHECK_EQ(s.day_number, day);  // echoes the requested day
    CHECK_NEAR(s.target_ph_min, 5.8f, 1e-5);
    CHECK_NEAR(s.target_ph_max, 6.2f, 1e-5);
    CHECK_EQ(s.nutrient_A_ml_per_liter, 0.0f);
    CHECK_EQ(s.nutrient_B_ml_per_liter, 0.0f);
    CHECK_EQ(s.nutrient_C_ml_per_liter, 0.0f);
    CHECK_EQ(s.light_on_time, 960);
    CHECK_EQ(s.light_off_time, 480);
    CHECK_EQ(s.ec_target, 0.0f);  // 0 = EC feeding disabled
    // SUSPECT: the fallback tolerance is 0.20 in a field that is uS/cm everywhere else
    // (parsed days carry 100..200). Harmless today because ec_target == 0 disables EC feeding.
    CHECK_NEAR(s.ec_tolerance, 0.20f, 1e-6);
  }
}

TEST(calendar, default_constructed_schedule) {
  DailySchedule s;
  CHECK_EQ(s.day_number, 0);
  CHECK_NEAR(s.target_ph_min, 5.8f, 1e-5);
  CHECK_NEAR(s.target_ph_max, 6.2f, 1e-5);
  CHECK_EQ(s.light_on_time, 960);
  CHECK_EQ(s.light_off_time, 480);
  CHECK_EQ(s.ec_target, 0.0f);
  CHECK_NEAR(s.ec_tolerance, 0.20f, 1e-6);  // same unit oddity as above
}

// ---------------------------------------------------------------------------
// JSON parse edge cases (synthetic JSON through the real parser)
// ---------------------------------------------------------------------------

TEST(calendar, missing_fields_get_parse_defaults) {
  CalendarFixture f(R"([{"day": 5}])");
  CHECK_EQ(f.cal.schedule_map_.size(), 1u);
  DailySchedule s = f.cal.get_schedule(5);
  CHECK_NEAR(s.target_ph_min, 5.8f, 1e-5);
  CHECK_NEAR(s.target_ph_max, 6.2f, 1e-5);
  CHECK_EQ(s.nutrient_A_ml_per_liter, 0.0f);
  CHECK_EQ(s.light_on_time, 960);
  CHECK_EQ(s.light_off_time, 480);
  CHECK_EQ(s.ec_target, 0.0f);
  // A parsed day without ec_tolerance gets 0.20 mS/cm -> 200 uS/cm (unlike the lookup fallback).
  CHECK_NEAR(s.ec_tolerance, 200.0f, 0.01);
}

TEST(calendar, invalid_day_numbers_are_skipped) {
  CalendarFixture f(R"([{"day": 0}, {"day": 121}, {"day": -1}, {"ph_min": 6.0}, {"day": 3, "ph_min": 6.0}])");
  CHECK_EQ(f.cal.schedule_map_.size(), 1u);
  CHECK_NEAR(f.cal.get_schedule(3).target_ph_min, 6.0f, 1e-5);
}

TEST(calendar, day_number_above_255_wraps_into_uint8) {
  // SUSPECT: `uint8_t day = obj["day"] | 0` truncates before the 1..120 range check,
  // so day 300 is stored as day 44 (300 - 256) instead of being rejected; 256 becomes 0 and is skipped.
  CalendarFixture f(R"([{"day": 300, "ph_min": 6.1}, {"day": 256, "ph_min": 6.3}])");
  CHECK_EQ(f.cal.schedule_map_.size(), 1u);
  CHECK(f.cal.schedule_map_.count(44) == 1);
  CHECK_NEAR(f.cal.get_schedule(44).target_ph_min, 6.1f, 1e-5);
}

TEST(calendar, duplicate_day_last_entry_wins) {
  CalendarFixture f(R"([{"day": 2, "ph_min": 5.0}, {"day": 2, "ph_min": 6.0}])");
  CHECK_EQ(f.cal.schedule_map_.size(), 1u);
  CHECK_NEAR(f.cal.get_schedule(2).target_ph_min, 6.0f, 1e-5);
}

TEST(calendar, parse_failures_leave_table_empty) {
  for (const char *json : {"", "not json", R"({"day": 1})", "[]", R"([{"day": 0}])", R"([{"day": 1})"}) {
    CalendarManager cal;
    cal.set_schedule_json(json);
    CHECK(!cal.parse_schedule_json());
    CHECK_EQ(cal.schedule_map_.size(), 0u);
    // Lookup still works and returns defaults.
    CHECK_EQ(cal.get_schedule(1).ec_target, 0.0f);
  }
}

TEST(calendar, parse_returns_true_with_at_least_one_valid_day) {
  CalendarManager cal;
  cal.set_schedule_json(R"([{"day": 0}, {"day": 7}])");
  CHECK(cal.parse_schedule_json());
}

// ---------------------------------------------------------------------------
// Day counter + NVS
// ---------------------------------------------------------------------------

TEST(calendar, first_boot_starts_at_day_1) {
  CalendarFixture f;
  CHECK_EQ(f.cal.get_current_day(), 1);
  CHECK_EQ(f.cal.last_synced_date_, 0);
  CHECK_EQ(f.cal.get_today_schedule().day_number, 1);
}

TEST(calendar, set_current_day_validates_range) {
  CalendarFixture f;
  CHECK(f.cal.set_current_day(1));
  CHECK(f.cal.set_current_day(120));
  CHECK_EQ(f.cal.get_current_day(), 120);
  CHECK(!f.cal.set_current_day(0));
  CHECK(!f.cal.set_current_day(121));
  CHECK(!f.cal.set_current_day(255));
  CHECK_EQ(f.cal.get_current_day(), 120);  // unchanged by rejected values
}

TEST(calendar, current_day_survives_reboot) {
  {
    CalendarFixture f;
    CHECK(f.cal.set_current_day(69));
  }
  CalendarFixture after_reboot;  // same fake NVS
  CHECK_EQ(after_reboot.cal.get_current_day(), 69);
  CHECK_EQ(after_reboot.cal.get_today_schedule().day_number, 69);
}

TEST(calendar, invalid_day_in_nvs_resets_to_1) {
  uint8_t bad = 200;
  esphome::global_preferences->make_preference<uint8_t>(esphome::fnv1_hash("current_day")).save(&bad);
  CalendarFixture f;
  CHECK_EQ(f.cal.get_current_day(), 1);
  CalendarFixture again;
  CHECK_EQ(again.cal.get_current_day(), 1);  // the reset was written back
}

TEST(calendar, advance_and_go_back_wrap_at_cycle_ends) {
  CalendarFixture f;
  f.cal.set_current_day(119);
  CHECK(f.cal.advance_day());
  CHECK_EQ(f.cal.get_current_day(), 120);
  CHECK(f.cal.advance_day());
  CHECK_EQ(f.cal.get_current_day(), 1);  // wraps, does not stop at 120
  CHECK(f.cal.go_back_day());
  CHECK_EQ(f.cal.get_current_day(), 120);
  CHECK(f.cal.go_back_day());
  CHECK_EQ(f.cal.get_current_day(), 119);
  CHECK(f.cal.reset_to_day_1());
  CHECK_EQ(f.cal.get_current_day(), 1);
}

TEST(calendar, nvs_write_failure_still_changes_day_in_ram) {
  CalendarFixture f;
  host::nvs_fail_writes = true;
  CHECK(!f.cal.set_current_day(10));       // reports the failed save ...
  CHECK_EQ(f.cal.get_current_day(), 10);   // ... but the in-memory day is already changed
}

TEST(calendar, safe_mode_flag) {
  CalendarFixture f;
  CHECK(!f.cal.is_safe_mode());
  f.cal.toggle_safe_mode();
  CHECK(f.cal.is_safe_mode());
  f.cal.set_safe_mode_enabled(false);
  CHECK(!f.cal.is_safe_mode());
}

// ---------------------------------------------------------------------------
// sync_to_date (real local date -> grow day)
// ---------------------------------------------------------------------------

TEST(calendar_sync, invalid_ordinal_is_ignored) {
  CalendarFixture f;
  CHECK(!f.cal.sync_to_date(0));
  CHECK(!f.cal.sync_to_date(-5));
  CHECK_EQ(f.cal.last_synced_date_, 0);
  CHECK_EQ(f.cal.get_current_day(), 1);
}

TEST(calendar_sync, first_sync_seeds_without_advancing) {
  CalendarFixture f;
  f.cal.set_current_day(10);
  CHECK(!f.cal.sync_to_date(20771));
  CHECK_EQ(f.cal.last_synced_date_, 20771);
  CHECK_EQ(f.cal.get_current_day(), 10);
}

TEST(calendar_sync, same_day_is_noop_and_next_day_advances) {
  CalendarFixture f;
  f.cal.set_current_day(10);
  f.cal.sync_to_date(20771);
  CHECK(!f.cal.sync_to_date(20771));
  CHECK_EQ(f.cal.get_current_day(), 10);
  CHECK(f.cal.sync_to_date(20772));
  CHECK_EQ(f.cal.get_current_day(), 11);
  CHECK_EQ(f.cal.last_synced_date_, 20772);
}

TEST(calendar_sync, catches_up_several_days_after_power_off) {
  CalendarFixture f;
  f.cal.set_current_day(10);
  f.cal.sync_to_date(20771);
  CHECK(f.cal.sync_to_date(20776));
  CHECK_EQ(f.cal.get_current_day(), 15);
}

TEST(calendar_sync, wraps_past_day_120) {
  CalendarFixture f;
  f.cal.set_current_day(119);
  f.cal.sync_to_date(20771);
  CHECK(f.cal.sync_to_date(20776));  // +5
  CHECK_EQ(f.cal.get_current_day(), 4);
  // A gap longer than a whole cycle is taken modulo 120: +250 days from day 4 -> day 14.
  CHECK(f.cal.sync_to_date(20776 + 250));
  CHECK_EQ(f.cal.get_current_day(), 14);
}

TEST(calendar_sync, clock_going_backwards_resyncs_without_rewind) {
  CalendarFixture f;
  f.cal.set_current_day(10);
  f.cal.sync_to_date(20771);
  CHECK(!f.cal.sync_to_date(20761));
  CHECK_EQ(f.cal.get_current_day(), 10);
  CHECK_EQ(f.cal.last_synced_date_, 20761);
  // Consequence: when the clock recovers, the 10 days are counted again.
  CHECK(f.cal.sync_to_date(20771));
  CHECK_EQ(f.cal.get_current_day(), 20);
}

TEST(calendar_sync, manual_offset_is_preserved) {
  CalendarFixture f;
  f.cal.sync_to_date(20771);
  f.cal.set_current_day(50);  // owner corrects the day by hand
  CHECK(f.cal.sync_to_date(20772));
  CHECK_EQ(f.cal.get_current_day(), 51);
}

TEST(calendar_sync, last_synced_date_survives_reboot) {
  {
    CalendarFixture f;
    f.cal.set_current_day(10);
    f.cal.sync_to_date(20771);
  }
  CalendarFixture after_reboot;
  CHECK_EQ(after_reboot.cal.last_synced_date_, 20771);
  CHECK(after_reboot.cal.sync_to_date(20773));  // off for two midnights
  CHECK_EQ(after_reboot.cal.get_current_day(), 12);
}
