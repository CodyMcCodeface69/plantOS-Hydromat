// CalendarManager: phase tables generated from plantOS.yaml, phase lookup (hold the
// last phase), unit conversion, per-calendar day counter + NVS (incl. migration from
// the single-calendar keys), real-date sync, active-calendar switching.
// Real code under test: components/calendar_manager/{CalendarManager.{h,cpp},phase_table.h}
// The Python validation is tested by tools/test_phase_tables.py (`make pytest`).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include "minitest.h"
#include "fixture.h"

using esphome::calendar_manager::CAL_ANZUCHT;
using esphome::calendar_manager::CAL_HAUPT;
using esphome::calendar_manager::CAL_MUTTER;
using esphome::calendar_manager::CalendarDef;
using esphome::calendar_manager::CalendarManager;
using esphome::calendar_manager::DailySchedule;
using esphome::calendar_manager::MAX_DAY;
using esphome::calendar_manager::Phase;
using esphome::calendar_manager::find_phase;
using esphome::calendar_manager::schedule_for_day;

static bool same_values(const DailySchedule &a, const DailySchedule &b) {
  return a.target_ph_min == b.target_ph_min && a.target_ph_max == b.target_ph_max &&
         a.nutrient_A_ml_per_liter == b.nutrient_A_ml_per_liter &&
         a.nutrient_B_ml_per_liter == b.nutrient_B_ml_per_liter &&
         a.nutrient_C_ml_per_liter == b.nutrient_C_ml_per_liter && a.light_on_time == b.light_on_time &&
         a.light_off_time == b.light_off_time && a.ec_target == b.ec_target && a.ec_tolerance == b.ec_tolerance;
}

// ---------------------------------------------------------------------------
// Production Haupt calendar (generated from plantOS.yaml at build time)
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

// The Haupt phases must reproduce exactly the 120 rows of the former schedule_json.
// Regenerate with `make golden` only when the schedule is changed on purpose.
TEST(calendar, golden_haupt_schedule_all_120_days) {
  CalendarFixture f;
  CHECK_EQ(f.cal.get_active_calendar(), CAL_HAUPT);
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

TEST(calendar, production_tables_shape) {
  const CalendarDef *c = production_calendars();
  CHECK_EQ(std::string(c[CAL_ANZUCHT].name), std::string("Anzucht"));
  CHECK_EQ(std::string(c[CAL_MUTTER].name), std::string("Mutter"));
  CHECK_EQ(std::string(c[CAL_HAUPT].name), std::string("Haupt"));
  CHECK_EQ(c[CAL_HAUPT].phase_count, 18);
  CHECK_EQ(c[CAL_HAUPT].days, 120);
  CHECK_EQ(c[CAL_MUTTER].phase_count, 1);
  CHECK_EQ(c[CAL_MUTTER].days, 0);  // open-ended
  for (int i = 0; i < 3; i++) {
    REQUIRE(c[i].phase_count >= 1);
    CHECK_EQ(c[i].phases[0].from_day, 1);
    for (int p = 1; p < c[i].phase_count; p++) CHECK(c[i].phases[p].from_day > c[i].phases[p - 1].from_day);
  }
}

TEST(calendar, ec_is_converted_from_mS_to_uS) {
  CalendarFixture f;
  // Haupt day 1: ec_target 0.50, ec_tolerance 0.10 (mS/cm)
  CHECK_NEAR(f.cal.get_schedule(1).ec_target, 500.0f, 0.01);
  CHECK_NEAR(f.cal.get_schedule(1).ec_tolerance, 100.0f, 0.01);
  // day 11: 1.20 / 0.20
  CHECK_NEAR(f.cal.get_schedule(11).ec_target, 1200.0f, 0.01);
  CHECK_NEAR(f.cal.get_schedule(11).ec_tolerance, 200.0f, 0.01);
  // day 60: 2.2 / 0.2
  CHECK_NEAR(f.cal.get_schedule(60).ec_target, 2200.0f, 0.01);
  // day 120 (flush): 0.15 / 0.15
  CHECK_NEAR(f.cal.get_schedule(120).ec_target, 150.0f, 0.01);
  CHECK_NEAR(f.cal.get_schedule(120).ec_tolerance, 150.0f, 0.01);
}

TEST(calendar, spot_values_of_production_days) {
  CalendarFixture f;
  DailySchedule d1 = f.cal.get_schedule(1);
  CHECK_EQ(d1.day_number, 1);
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
  CHECK_EQ(d60.day_number, 60);
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

TEST(calendar, phase_names_and_lengths) {
  CalendarFixture f;
  CHECK_EQ(std::string(f.cal.get_calendar_name()), std::string("Haupt"));
  CHECK_EQ(f.cal.get_defined_days(), 120);
  CHECK_EQ(std::string(f.cal.get_phase_name()), std::string("Keimling / S\xc3\xa4mling"));  // day 1
  f.cal.set_current_day(43);
  CHECK_EQ(std::string(f.cal.get_phase_name()), std::string("Vegetation bis fr\xc3\xbche Bl\xc3\xbcte"));
  f.cal.set_current_day(120);
  CHECK_EQ(std::string(f.cal.get_phase_name()), std::string("Flush"));
  f.cal.set_current_day(500);
  CHECK_EQ(std::string(f.cal.get_phase_name()), std::string("Flush"));  // held
  f.cal.set_active_calendar(CAL_MUTTER);
  CHECK_EQ(std::string(f.cal.get_calendar_name()), std::string("Mutter"));
  CHECK_EQ(f.cal.get_defined_days(), 0);
  f.cal.set_active_calendar(CAL_ANZUCHT);
  CHECK_EQ(f.cal.get_defined_days(), 21);
}

// ---------------------------------------------------------------------------
// Lookup: hold the last phase, day 0 defaults
// ---------------------------------------------------------------------------

// CHANGED (owner decision "hold the last phase"): days past 120 used to return the
// lookup defaults (EC off, no doses, light 16:00-08:00); now the flush phase holds.
TEST(calendar, haupt_holds_flush_phase_after_day_120) {
  CalendarFixture f;
  DailySchedule d120 = f.cal.get_schedule(120);
  for (int day : {121, 200, 255, 256, 300, 1000, (int) MAX_DAY}) {
    DailySchedule s = f.cal.get_schedule(day);
    CHECK_EQ(s.day_number, day);  // echoes the requested day (no uint8 wrap any more)
    CHECK(same_values(s, d120));
  }
}

TEST(calendar, day_0_returns_defaults) {
  CalendarFixture f;
  DailySchedule s = f.cal.get_schedule(0);
  CHECK_EQ(s.day_number, 0);
  CHECK_NEAR(s.target_ph_min, 5.8f, 1e-5);
  CHECK_NEAR(s.target_ph_max, 6.2f, 1e-5);
  CHECK_EQ(s.nutrient_A_ml_per_liter, 0.0f);
  CHECK_EQ(s.nutrient_B_ml_per_liter, 0.0f);
  CHECK_EQ(s.nutrient_C_ml_per_liter, 0.0f);
  CHECK_EQ(s.light_on_time, 960);
  CHECK_EQ(s.light_off_time, 480);
  CHECK_EQ(s.ec_target, 0.0f);  // 0 = EC feeding disabled
  // FIXED: the fallback tolerance was 0.20 in a uS/cm field; it is 200 uS/cm (0.20 mS/cm) now.
  CHECK_NEAR(s.ec_tolerance, 200.0f, 1e-3);
}

TEST(calendar, default_constructed_schedule) {
  DailySchedule s;
  CHECK_EQ(s.day_number, 0);
  CHECK_NEAR(s.target_ph_min, 5.8f, 1e-5);
  CHECK_NEAR(s.target_ph_max, 6.2f, 1e-5);
  CHECK_EQ(s.light_on_time, 960);
  CHECK_EQ(s.light_off_time, 480);
  CHECK_EQ(s.ec_target, 0.0f);
  CHECK_NEAR(s.ec_tolerance, 200.0f, 1e-3);  // FIXED: was 0.20
}

TEST(calendar, without_tables_everything_is_default) {
  CalendarManager cal;  // set_calendars() never called
  cal.setup();
  CHECK_EQ(cal.get_schedule(5).ec_target, 0.0f);
  CHECK_EQ(cal.get_schedule(5).day_number, 5);
  CHECK_EQ(std::string(cal.get_calendar_name()), std::string(""));
  CHECK_EQ(std::string(cal.get_phase_name()), std::string(""));
  CHECK_EQ(cal.get_defined_days(), 0);
}

// The pure lookup, on a synthetic table.
static const Phase THREE_PHASES[] = {
    {1, 5.8f, 6.2f, 1.0f, 0.0f, 0.0f, 960, 480, 0.5f, 0.1f, "one"},
    {10, 5.9f, 6.3f, 2.0f, 0.0f, 0.0f, 960, 480, 1.0f, 0.2f, "ten"},
    {20, 6.0f, 6.4f, 3.0f, 0.0f, 0.0f, 600, 600, 1.5f, 0.3f, "twenty"},
};

TEST(calendar_lookup, last_phase_with_from_day_not_after_day) {
  CHECK(find_phase(THREE_PHASES, 3, 0) == nullptr);
  CHECK(find_phase(THREE_PHASES, 3, 1) == &THREE_PHASES[0]);
  CHECK(find_phase(THREE_PHASES, 3, 9) == &THREE_PHASES[0]);
  CHECK(find_phase(THREE_PHASES, 3, 10) == &THREE_PHASES[1]);
  CHECK(find_phase(THREE_PHASES, 3, 19) == &THREE_PHASES[1]);
  CHECK(find_phase(THREE_PHASES, 3, 20) == &THREE_PHASES[2]);
  CHECK(find_phase(THREE_PHASES, 3, 65535) == &THREE_PHASES[2]);  // hold
  CHECK(find_phase(THREE_PHASES, 0, 5) == nullptr);
  CHECK(find_phase(nullptr, 3, 5) == nullptr);
  CHECK(find_phase(THREE_PHASES, 1, 50) == &THREE_PHASES[0]);  // single endless phase
}

TEST(calendar_lookup, schedule_for_day_converts_and_echoes_day) {
  CalendarDef def{"T", THREE_PHASES, 3, 0};
  DailySchedule s = schedule_for_day(&def, 15);
  CHECK_EQ(s.day_number, 15);
  CHECK_NEAR(s.target_ph_min, 5.9f, 1e-6);
  CHECK_NEAR(s.nutrient_A_ml_per_liter, 2.0f, 1e-6);
  CHECK_NEAR(s.ec_target, 1000.0f, 1e-3);
  CHECK_NEAR(s.ec_tolerance, 200.0f, 1e-3);
  DailySchedule none = schedule_for_day(nullptr, 15);
  CHECK_EQ(none.day_number, 15);
  CHECK_EQ(none.ec_target, 0.0f);
}

// ---------------------------------------------------------------------------
// Placeholder calendars
// ---------------------------------------------------------------------------

TEST(calendar, mutter_is_one_endless_phase) {
  CalendarFixture f;
  f.cal.set_active_calendar(CAL_MUTTER);
  DailySchedule d1 = f.cal.get_schedule(1);
  CHECK(d1.ec_target > 0.0f);
  CHECK(d1.nutrient_A_ml_per_liter > 0.0f);
  for (int day : {2, 120, 365, 3000, (int) MAX_DAY}) CHECK(same_values(f.cal.get_schedule(day), d1));
}

TEST(calendar, anzucht_has_no_nutrients_in_its_first_phase) {
  CalendarFixture f;
  f.cal.set_active_calendar(CAL_ANZUCHT);
  DailySchedule d1 = f.cal.get_schedule(1);
  CHECK_EQ(d1.nutrient_A_ml_per_liter, 0.0f);
  CHECK_EQ(d1.nutrient_B_ml_per_liter, 0.0f);
  CHECK_EQ(d1.nutrient_C_ml_per_liter, 0.0f);
  CHECK_EQ(d1.ec_target, 0.0f);  // EC feeding off
  CHECK(same_values(f.cal.get_schedule(7), d1));
  CHECK(!same_values(f.cal.get_schedule(8), d1));
  CHECK(same_values(f.cal.get_schedule(100), f.cal.get_schedule(8)));  // held
}

// ---------------------------------------------------------------------------
// Day counter + NVS
// ---------------------------------------------------------------------------

TEST(calendar, first_boot_starts_at_day_1) {
  CalendarFixture f;
  CHECK_EQ(f.cal.get_current_day(), 1);
  CHECK_EQ(synced_date(f.cal), 0);
  CHECK_EQ(f.cal.get_today_schedule().day_number, 1);
  for (int i = 0; i < 3; i++) {
    CHECK_EQ(f.cal.slots_[i].day, 1);
    CHECK_EQ(f.cal.slots_[i].last_sync_date, 0);
  }
}

// CHANGED: the valid range is 1..MAX_DAY (was 1..120).
TEST(calendar, set_current_day_validates_range) {
  CalendarFixture f;
  CHECK(f.cal.set_current_day(1));
  CHECK(f.cal.set_current_day(120));
  CHECK(f.cal.set_current_day(121));
  CHECK(f.cal.set_current_day(300));
  CHECK(f.cal.set_current_day(MAX_DAY));
  CHECK_EQ(f.cal.get_current_day(), MAX_DAY);
  CHECK(!f.cal.set_current_day(0));
  CHECK(!f.cal.set_current_day(MAX_DAY + 1));
  CHECK_EQ(f.cal.get_current_day(), MAX_DAY);  // unchanged by rejected values
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

TEST(calendar, day_above_255_survives_reboot) {
  {
    CalendarFixture f;
    CHECK(f.cal.set_current_day(400));
  }
  CalendarFixture after_reboot;
  CHECK_EQ(after_reboot.cal.get_current_day(), 400);
}

TEST(calendar, invalid_day_in_nvs_slot_resets_to_1) {
  for (uint16_t bad : {(uint16_t) 0, (uint16_t) (MAX_DAY + 1)}) {
    host::reset();
    CalendarManager::SlotRecord rec{bad, 1, 20771};
    esphome::global_preferences->make_preference<CalendarManager::SlotRecord>(esphome::fnv1_hash("calendar_slot_2"))
        .save(&rec);
    CalendarFixture f;
    CHECK_EQ(f.cal.get_current_day(), 1);
    CHECK_EQ(synced_date(f.cal), 20771);  // the date is kept
    CalendarFixture again;
    CHECK_EQ(again.cal.get_current_day(), 1);  // the reset was written back
  }
}

// CHANGED: no wrap at the cycle ends. Was: 120 -> advance -> 1 and 1 -> back -> 120.
TEST(calendar, advance_counts_past_120_and_go_back_stops_at_1) {
  CalendarFixture f;
  f.cal.set_current_day(119);
  CHECK(f.cal.advance_day());
  CHECK_EQ(f.cal.get_current_day(), 120);
  CHECK(f.cal.advance_day());
  CHECK_EQ(f.cal.get_current_day(), 121);
  CHECK(f.cal.go_back_day());
  CHECK_EQ(f.cal.get_current_day(), 120);
  CHECK(f.cal.reset_to_day_1());
  CHECK_EQ(f.cal.get_current_day(), 1);
  CHECK(!f.cal.go_back_day());
  CHECK_EQ(f.cal.get_current_day(), 1);
  f.cal.set_current_day(MAX_DAY);
  CHECK(!f.cal.advance_day());
  CHECK_EQ(f.cal.get_current_day(), MAX_DAY);
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
// Per-calendar slots, switching
// ---------------------------------------------------------------------------

TEST(calendar_slots, default_active_calendar_is_haupt) {
  CalendarFixture f;
  CHECK_EQ(f.cal.get_active_calendar(), CAL_HAUPT);
  CHECK(!f.cal.set_active_calendar(3));
  CHECK_EQ(f.cal.get_active_calendar(), CAL_HAUPT);
}

TEST(calendar_slots, each_calendar_keeps_its_own_day) {
  CalendarFixture f;
  f.cal.set_current_day(69);
  CHECK(f.cal.set_active_calendar(CAL_MUTTER));
  CHECK_EQ(f.cal.get_current_day(), 1);
  f.cal.set_current_day(300);
  CHECK(f.cal.set_active_calendar(CAL_ANZUCHT));
  CHECK_EQ(f.cal.get_current_day(), 1);
  f.cal.advance_day();
  CHECK(f.cal.set_active_calendar(CAL_HAUPT));
  CHECK_EQ(f.cal.get_current_day(), 69);
  // The schedule follows the active calendar: Haupt day 69 vs. Mutter day 300.
  CHECK_NEAR(f.cal.get_today_schedule().ec_target, 2200.0f, 0.01);
  f.cal.set_active_calendar(CAL_MUTTER);
  CHECK_EQ(f.cal.get_current_day(), 300);
  CHECK_NEAR(f.cal.get_today_schedule().ec_target, 1600.0f, 0.01);

  // All three survive a reboot; the active index is not persisted here (3.3's job).
  CalendarFixture after_reboot;
  CHECK_EQ(after_reboot.cal.get_active_calendar(), CAL_HAUPT);
  CHECK_EQ(after_reboot.cal.get_current_day(), 69);
  after_reboot.cal.set_active_calendar(CAL_MUTTER);
  CHECK_EQ(after_reboot.cal.get_current_day(), 300);
  after_reboot.cal.set_active_calendar(CAL_ANZUCHT);
  CHECK_EQ(after_reboot.cal.get_current_day(), 2);
}

TEST(calendar_slots, switching_back_resumes_by_date) {
  CalendarFixture f;
  f.cal.set_current_day(10);
  f.cal.sync_to_date(20771);              // Haupt seeded at day 10
  f.cal.set_active_calendar(CAL_MUTTER);
  CHECK(!f.cal.sync_to_date(20771));      // Mutter's first sync only seeds
  CHECK(f.cal.sync_to_date(20776));       // +5
  CHECK_EQ(f.cal.get_current_day(), 6);
  f.cal.set_active_calendar(CAL_HAUPT);
  CHECK(f.cal.sync_to_date(20778));       // Haupt: 7 days since its last sync
  CHECK_EQ(f.cal.get_current_day(), 17);
  f.cal.set_active_calendar(CAL_MUTTER);
  CHECK(f.cal.sync_to_date(20778));       // Mutter: 2 more days
  CHECK_EQ(f.cal.get_current_day(), 8);
}

TEST(calendar_slots, slot_record_is_8_bytes_per_calendar) {
  CalendarFixture f;
  f.cal.set_current_day(42);
  for (const char *key : {"calendar_slot_0", "calendar_slot_1", "calendar_slot_2"}) {
    auto it = host::nvs.find(esphome::fnv1_hash(key));
    REQUIRE(it != host::nvs.end());
    CHECK_EQ(it->second.size(), 8u);
  }
  CalendarManager::SlotRecord rec{};
  memcpy(&rec, host::nvs[esphome::fnv1_hash("calendar_slot_2")].data(), 8);
  CHECK_EQ(rec.day, 42);
  CHECK_EQ(rec.version, 1);
}

// ---------------------------------------------------------------------------
// Migration from the single-calendar NVS keys of older firmware
// ---------------------------------------------------------------------------

static void save_legacy(const uint8_t *day, const int32_t *date) {
  if (day) esphome::global_preferences->make_preference<uint8_t>(esphome::fnv1_hash("current_day")).save(day);
  if (date) esphome::global_preferences->make_preference<int32_t>(esphome::fnv1_hash("last_sync_date")).save(date);
}

TEST(calendar_migration, old_day_and_date_move_into_haupt) {
  uint8_t day = 69;
  int32_t date = 20771;
  save_legacy(&day, &date);
  {
    CalendarFixture f;
    CHECK_EQ(f.cal.get_active_calendar(), CAL_HAUPT);
    CHECK_EQ(f.cal.get_current_day(), 69);
    CHECK_EQ(synced_date(f.cal), 20771);
    CHECK_EQ(f.cal.slots_[CAL_MUTTER].day, 1);
    CHECK_EQ(f.cal.slots_[CAL_ANZUCHT].day, 1);
    CHECK(f.cal.sync_to_date(20772));  // the next midnight continues the grow
    CHECK_EQ(f.cal.get_current_day(), 70);
  }
  // The legacy keys are left untouched (an old firmware flashed back catches up by date) ...
  uint8_t legacy_day = 0;
  CHECK(esphome::global_preferences->make_preference<uint8_t>(esphome::fnv1_hash("current_day")).load(&legacy_day));
  CHECK_EQ(legacy_day, 69);
  // ... and ignored once the slot exists.
  uint8_t other = 5;
  save_legacy(&other, nullptr);
  CalendarFixture after_reboot;
  CHECK_EQ(after_reboot.cal.get_current_day(), 70);
  CHECK_EQ(synced_date(after_reboot.cal), 20772);
}

TEST(calendar_migration, only_a_date_stored) {
  int32_t date = 20700;
  save_legacy(nullptr, &date);
  CalendarFixture f;
  CHECK_EQ(f.cal.get_current_day(), 1);
  CHECK_EQ(synced_date(f.cal), 20700);
}

TEST(calendar_migration, invalid_old_day_becomes_day_1) {
  uint8_t day = 200;  // the old firmware reset anything outside 1..120 to 1
  int32_t date = 20771;
  save_legacy(&day, &date);
  CalendarFixture f;
  CHECK_EQ(f.cal.get_current_day(), 1);
  CHECK_EQ(synced_date(f.cal), 20771);
}

TEST(calendar_migration, nothing_stored_is_a_fresh_start) {
  CalendarFixture f;
  CHECK_EQ(f.cal.get_current_day(), 1);
  CHECK_EQ(synced_date(f.cal), 0);
  CHECK(host::nvs.find(esphome::fnv1_hash("current_day")) == host::nvs.end());  // never written
}

// ---------------------------------------------------------------------------
// restart_grow
// ---------------------------------------------------------------------------

TEST(calendar_restart, day_1_as_of_the_last_synced_date) {
  CalendarFixture f;
  f.cal.set_current_day(50);
  f.cal.sync_to_date(20771);
  CHECK(f.cal.restart_grow());
  CHECK_EQ(f.cal.get_current_day(), 1);
  CHECK_EQ(synced_date(f.cal), 20771);
  CHECK(!f.cal.sync_to_date(20771));  // same day: still day 1
  CHECK(f.cal.sync_to_date(20772));
  CHECK_EQ(f.cal.get_current_day(), 2);
}

TEST(calendar_restart, before_any_sync_the_next_sync_seeds) {
  CalendarFixture f;
  f.cal.set_current_day(50);
  f.cal.slots_[CAL_HAUPT].last_sync_date = 20000;  // stale date from an earlier boot
  CHECK(f.cal.restart_grow());                     // no date seen in this boot yet
  CHECK_EQ(synced_date(f.cal), 0);
  CHECK(!f.cal.sync_to_date(20771));  // seeds, no advance
  CHECK_EQ(f.cal.get_current_day(), 1);
}

TEST(calendar_restart, only_the_active_calendar_restarts) {
  CalendarFixture f;
  f.cal.set_current_day(50);
  f.cal.set_active_calendar(CAL_MUTTER);
  f.cal.set_current_day(400);
  f.cal.restart_grow();
  CHECK_EQ(f.cal.get_current_day(), 1);
  f.cal.set_active_calendar(CAL_HAUPT);
  CHECK_EQ(f.cal.get_current_day(), 50);
  CalendarFixture after_reboot;
  after_reboot.cal.set_active_calendar(CAL_MUTTER);
  CHECK_EQ(after_reboot.cal.get_current_day(), 1);
}

// ---------------------------------------------------------------------------
// sync_to_date (real local date -> grow day)
// ---------------------------------------------------------------------------

TEST(calendar_sync, invalid_ordinal_is_ignored) {
  CalendarFixture f;
  CHECK(!f.cal.sync_to_date(0));
  CHECK(!f.cal.sync_to_date(-5));
  CHECK_EQ(synced_date(f.cal), 0);
  CHECK_EQ(f.cal.get_current_day(), 1);
}

TEST(calendar_sync, first_sync_seeds_without_advancing) {
  CalendarFixture f;
  f.cal.set_current_day(10);
  CHECK(!f.cal.sync_to_date(20771));
  CHECK_EQ(synced_date(f.cal), 20771);
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
  CHECK_EQ(synced_date(f.cal), 20772);
}

TEST(calendar_sync, catches_up_several_days_after_power_off) {
  CalendarFixture f;
  f.cal.set_current_day(10);
  f.cal.sync_to_date(20771);
  CHECK(f.cal.sync_to_date(20776));
  CHECK_EQ(f.cal.get_current_day(), 15);
}

// CHANGED: was "wraps_past_day_120" (119 + 5 -> 4, gaps taken modulo 120).
TEST(calendar_sync, counts_past_day_120_without_wrap) {
  CalendarFixture f;
  f.cal.set_current_day(119);
  f.cal.sync_to_date(20771);
  CHECK(f.cal.sync_to_date(20776));  // +5
  CHECK_EQ(f.cal.get_current_day(), 124);
  CHECK(f.cal.sync_to_date(20776 + 250));
  CHECK_EQ(f.cal.get_current_day(), 374);
  // A huge jump is capped at MAX_DAY.
  CHECK(f.cal.sync_to_date(20776 + 250 + 20000));
  CHECK_EQ(f.cal.get_current_day(), MAX_DAY);
}

TEST(calendar_sync, clock_going_backwards_resyncs_without_rewind) {
  CalendarFixture f;
  f.cal.set_current_day(10);
  f.cal.sync_to_date(20771);
  CHECK(!f.cal.sync_to_date(20761));
  CHECK_EQ(f.cal.get_current_day(), 10);
  CHECK_EQ(synced_date(f.cal), 20761);
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
  CHECK_EQ(synced_date(after_reboot.cal), 20771);
  CHECK(after_reboot.cal.sync_to_date(20773));  // off for two midnights
  CHECK_EQ(after_reboot.cal.get_current_day(), 12);
}
