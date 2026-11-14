// Time-driven decisions of the controller.
// Real code under test: components/plantos_controller/controller.cpp
//   isNightModeHours, getLocalDateOrdinal,
//   shouldTriggerAutoFeeding, shouldTriggerAutoReservoirChange, getCurrentWeekNumber,
//   getCurrentDayOfWeek, and the pH slot schedule / date sync / auto-feed parts of handleIdle
#include "minitest.h"
#include "fixture.h"

using plantos_controller::ControllerState;

static bool night_at(ControllerFixture &f, int h, int m) {
  f.set_time(2026, 11, 14, h, m);
  return f.ctrl.isNightModeHours();
}

// ---------------------------------------------------------------------------
// isNightModeHours
// ---------------------------------------------------------------------------

TEST(night_mode, false_without_valid_time) {
  ControllerFixture f(false);
  CHECK(!f.ctrl.isNightModeHours());  // clock never set -> invalid
  f.set_time(2026, 11, 14, 23, 0);
  CHECK(f.ctrl.isNightModeHours());
  f.clock.set_invalid();
  CHECK(!f.ctrl.isNightModeHours());
  f.ctrl.setTimeSource(nullptr);
  CHECK(!f.ctrl.isNightModeHours());
}

TEST(night_mode, hour_window_wraps_past_midnight_default_22_to_9) {
  ControllerFixture f(false);  // no calendar -> YAML hour window
  CHECK(!night_at(f, 21, 59));
  CHECK(night_at(f, 22, 0));
  CHECK(night_at(f, 23, 59));
  CHECK(night_at(f, 0, 0));
  CHECK(night_at(f, 8, 59));
  CHECK(!night_at(f, 9, 0));
  CHECK(!night_at(f, 12, 0));
}

TEST(night_mode, hour_window_within_one_day) {
  ControllerFixture f(false);
  f.ctrl.setNightModeConfig(true, 1, 6);
  CHECK(!night_at(f, 0, 59));
  CHECK(night_at(f, 1, 0));
  CHECK(night_at(f, 5, 59));
  CHECK(!night_at(f, 6, 0));
  CHECK(!night_at(f, 23, 0));
}

TEST(night_mode, equal_start_and_end_hour_is_never_night) {
  ControllerFixture f(false);
  f.ctrl.setNightModeConfig(true, 8, 8);
  for (int h = 0; h < 24; h++) CHECK(!night_at(f, h, 30));
}

TEST(night_mode, enabled_flag_is_not_evaluated_here) {
  // Callers combine `night_mode_enabled_ && isNightModeHours()`.
  ControllerFixture f(false);
  f.ctrl.setNightModeConfig(false, 22, 9);
  CHECK(night_at(f, 23, 0));
}

TEST(night_mode, calendar_light_schedule_overrides_hour_window) {
  // Production day 1: light ON 16:00, OFF 08:00 (lit across midnight).
  // "Night" = light off = 08:00 .. 15:59, and the 22..9 hour window is ignored.
  ControllerFixture f;
  CHECK(!night_at(f, 7, 59));
  CHECK(night_at(f, 8, 0));
  CHECK(night_at(f, 12, 0));
  CHECK(night_at(f, 15, 59));
  CHECK(!night_at(f, 16, 0));
  CHECK(!night_at(f, 23, 0));  // inside 22..9, but the light is on
  CHECK(!night_at(f, 3, 0));
}

TEST(night_mode, calendar_schedule_of_the_calendar_day_counter_is_used) {
  ControllerFixture f;
  f.cal.set_current_day(60);  // light ON 16:00, OFF 04:00 -> dark 04:00 .. 15:59
  CHECK(!night_at(f, 3, 59));
  CHECK(night_at(f, 4, 0));
  CHECK(night_at(f, 7, 0));  // on day 1 the light would still be on
  CHECK(!night_at(f, 16, 0));
}

TEST(night_mode, calendar_day_schedule_not_wrapping) {
  ControllerFixture f(false);
  auto p = test_phase();
  p.light_on_min = 360;   // 06:00
  p.light_off_min = 1320; // 22:00
  OnePhaseCalendars cals(p);
  CalendarFixture c(cals.defs);
  f.ctrl.setCalendarManager(&c.cal);
  CHECK(night_at(f, 5, 59));
  CHECK(!night_at(f, 6, 0));
  CHECK(!night_at(f, 21, 59));
  CHECK(night_at(f, 22, 0));
  CHECK(night_at(f, 0, 0));
}

TEST(night_mode, equal_light_times_fall_back_to_hour_window) {
  ControllerFixture f(false);
  auto p = test_phase();
  p.light_on_min = 600;
  p.light_off_min = 600;
  OnePhaseCalendars cals(p);
  CalendarFixture c(cals.defs);
  f.ctrl.setCalendarManager(&c.cal);
  CHECK(night_at(f, 23, 0));
  CHECK(!night_at(f, 12, 0));
}

// ---------------------------------------------------------------------------
// getLocalDateOrdinal (days since 1970-01-01, local date)
// ---------------------------------------------------------------------------

TEST(date_ordinal, known_dates) {
  ControllerFixture f;
  struct { int y, m, d; int32_t ordinal; } cases[] = {
      {2019, 1, 1, 17897},   {2024, 2, 28, 19781}, {2024, 2, 29, 19782}, {2024, 3, 1, 19783},
      {2025, 12, 30, 20452}, {2025, 12, 31, 20453}, {2026, 1, 1, 20454},  {2026, 11, 14, 20771},
  };
  for (auto &c : cases) {
    f.set_time(c.y, c.m, c.d, 12, 0);
    CHECK_EQ(f.ctrl.getLocalDateOrdinal(), c.ordinal);
  }
}

TEST(date_ordinal, zero_without_valid_time) {
  ControllerFixture f;
  CHECK_EQ(f.ctrl.getLocalDateOrdinal(), 0);
  f.clock.set_local(2018, 12, 31, 12, 0);  // ESPTime::is_valid() needs year >= 2019
  CHECK_EQ(f.ctrl.getLocalDateOrdinal(), 0);
  f.ctrl.setTimeSource(nullptr);
  CHECK_EQ(f.ctrl.getLocalDateOrdinal(), 0);
}

TEST(date_ordinal, follows_local_date_not_utc) {
  ControllerFixture f;
  f.set_time(2026, 11, 15, 0, 30, 0, 3600);  // CET; UTC is still 2026-11-14 23:30
  CHECK_EQ(f.hal.timestamp / 86400, 20771);
  CHECK_EQ(f.ctrl.getLocalDateOrdinal(), 20772);
}

// ---------------------------------------------------------------------------
// shouldTriggerAutoFeeding
// ---------------------------------------------------------------------------

// Everything set so that auto-feeding triggers: IDLE, 18:00 (light on), level LOW, not fed today.
static void arm_auto_feed(ControllerFixture &f) {
  f.force_state(ControllerState::IDLE);
  f.set_time(2026, 11, 14, 18, 0);
  f.hal.level_high = false;
  f.hal.level_low = false;
  f.hal.level_empty = true;
  f.ctrl.auto_feeding_enabled_ = true;
}

TEST(auto_feed, triggers_when_all_conditions_hold) {
  ControllerFixture f;
  arm_auto_feed(f);
  CHECK(f.ctrl.shouldTriggerAutoFeeding());
}

TEST(auto_feed, each_condition_blocks) {
  {
    ControllerFixture f;
    arm_auto_feed(f);
    f.ctrl.auto_feeding_enabled_ = false;
    CHECK(!f.ctrl.shouldTriggerAutoFeeding());
  }
  for (ControllerState s : {ControllerState::NIGHT, ControllerState::PH_MIXING, ControllerState::PAUSE,
                            ControllerState::SHUTDOWN, ControllerState::WATER_FILLING}) {
    ControllerFixture f;
    arm_auto_feed(f);
    f.force_state(s);
    CHECK(!f.ctrl.shouldTriggerAutoFeeding());
  }
  {
    ControllerFixture f;
    arm_auto_feed(f);
    f.hal.has_level_sensors = false;
    CHECK(!f.ctrl.shouldTriggerAutoFeeding());
  }
  {
    ControllerFixture f;
    arm_auto_feed(f);
    f.clock.set_invalid();  // no date -> cannot enforce once-per-day
    CHECK(!f.ctrl.shouldTriggerAutoFeeding());
  }
  {
    plantos_controller::PlantOSController no_hal;
    no_hal.current_state_ = ControllerState::IDLE;
    CHECK(!no_hal.shouldTriggerAutoFeeding());
  }
}

TEST(auto_feed, water_level_pattern_must_be_empty_only) {
  // Trigger pattern is HIGH=off, LOW=off, EMPTY=on (the code calls this "LOW level").
  ControllerFixture f;
  arm_auto_feed(f);
  struct { bool high, low, empty, expect; } cases[] = {
      {false, false, true, true},  {false, false, false, false}, {false, true, true, false},
      {true, true, true, false},   {true, false, true, false},   {false, true, false, false},
  };
  for (auto &c : cases) {
    f.hal.level_high = c.high;
    f.hal.level_low = c.low;
    f.hal.level_empty = c.empty;
    CHECK_EQ(f.ctrl.shouldTriggerAutoFeeding(), c.expect);
  }
}

TEST(auto_feed, blocked_during_night_hours_unless_night_mode_disabled) {
  ControllerFixture f;
  arm_auto_feed(f);
  f.set_time(2026, 11, 14, 12, 0);  // day 1: light off 08:00-16:00 = night
  CHECK(!f.ctrl.shouldTriggerAutoFeeding());
  f.ctrl.setNightModeConfig(false, 22, 9);
  CHECK(f.ctrl.shouldTriggerAutoFeeding());
}

TEST(auto_feed, once_per_local_day) {
  ControllerFixture f;
  arm_auto_feed(f);
  f.ctrl.markAutoFeedDay(f.ctrl.getLocalDateOrdinal());  // what handleIdle stores
  CHECK_EQ(f.ctrl.last_auto_feed_day_, 20771);
  CHECK(!f.ctrl.shouldTriggerAutoFeeding());
  f.set_time(2026, 11, 14, 23, 59);
  CHECK(!f.ctrl.shouldTriggerAutoFeeding());
  f.set_time(2026, 11, 15, 0, 0);  // local midnight
  CHECK(f.ctrl.shouldTriggerAutoFeeding());
}

TEST(auto_feed, nvs_day_of_today_blocks) {
  ControllerFixture f;
  arm_auto_feed(f);
  f.psm.saveInt32("AutoFeedDay", 20771);  // 2026-11-14
  CHECK(!f.ctrl.shouldTriggerAutoFeeding());
  CHECK_EQ(f.ctrl.last_auto_feed_day_, 20771);
  // A stored earlier day does not block.
  host::reset();
  ControllerFixture g;
  arm_auto_feed(g);
  g.psm.saveInt32("AutoFeedDay", 20770);
  CHECK(g.ctrl.shouldTriggerAutoFeeding());
}

TEST(auto_feed, old_psm_marker_is_not_read) {
  // Firmware before 2026-11-14 wrote "AUTOFEED_<day*86400>" into the PSM event slot. That
  // marker was overwritten by startFeed() at once, so it never blocked anything; it is ignored.
  ControllerFixture f;
  arm_auto_feed(f);
  f.psm.logEvent("AUTOFEED_1794614400", 0);
  CHECK(f.ctrl.shouldTriggerAutoFeeding());
}

TEST(auto_feed, idle_handler_starts_feed_and_the_day_survives_a_reboot) {
  // Fixed 2026-11-14: the day is stored under its own NVS key (AutoFeedDay), so the
  // FEED_OPERATION event that startFeed() logs into the single PSM slot no longer erases it.
  ControllerFixture f;
  arm_auto_feed(f);
  f.ctrl.auto_ph_correction_enabled_ = false;  // keep the periodic pH check out of the way
  f.ctrl.handleIdle();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::FEED_FILLING);
  CHECK_EQ(f.ctrl.last_auto_feed_day_, 20771);
  CHECK_EQ(std::string(f.psm.getLastEvent().eventID), std::string("FEED_OPERATION"));
  CHECK(f.ctrl.auto_ph_correction_pending_);

  // "Reboot": new controller objects on the same NVS, same day, level LOW again.
  ControllerFixture after_reboot;
  arm_auto_feed(after_reboot);
  CHECK(!after_reboot.ctrl.shouldTriggerAutoFeeding());
  // Next local day it feeds again.
  after_reboot.set_time(2026, 11, 15, 18, 0);
  CHECK(after_reboot.ctrl.shouldTriggerAutoFeeding());
}

TEST(auto_feed, nvs_written_only_when_the_day_changes) {
  ControllerFixture f;
  arm_auto_feed(f);
  int before = host::nvs_save_count;
  f.ctrl.markAutoFeedDay(20771);
  CHECK_EQ(host::nvs_save_count, before + 1);
  f.ctrl.markAutoFeedDay(20771);
  f.ctrl.markAutoFeedDay(0);  // no date: ignored
  CHECK_EQ(host::nvs_save_count, before + 1);
  f.ctrl.markAutoFeedDay(20772);
  CHECK_EQ(host::nvs_save_count, before + 2);
}

// ---------------------------------------------------------------------------
// Auto reservoir change (weekly)
// ---------------------------------------------------------------------------

static void arm_auto_reservoir(ControllerFixture &f) {
  f.force_state(ControllerState::IDLE);
  f.set_time(2026, 11, 14, 18, 0);  // a Saturday
  f.ctrl.auto_reservoir_change_enabled_ = true;
  f.ctrl.auto_reservoir_change_day_ = 6;  // 0 = Sunday .. 6 = Saturday
}

TEST(auto_reservoir, day_of_week_and_week_number) {
  ControllerFixture f;
  CHECK_EQ(f.ctrl.getCurrentDayOfWeek(), 255);  // no time
  CHECK_EQ(f.ctrl.getCurrentWeekNumber(), 0);
  struct { int d; int dow; int64_t week; } cases[] = {
      {11, 3, 2966},  // Wednesday
      {12, 4, 2967},  // Thursday: the week number changes here (epoch day 0 was a Thursday)
      {14, 6, 2967},  // Saturday
      {15, 0, 2967},  // Sunday
      {18, 3, 2967},  // Wednesday
      {19, 4, 2968},
  };
  for (auto &c : cases) {
    f.set_time(2026, 11, c.d, 12, 0);
    CHECK_EQ(f.ctrl.getCurrentDayOfWeek(), c.dow);
    CHECK_EQ(f.ctrl.getCurrentWeekNumber(), c.week);
  }
}

TEST(auto_reservoir, day_of_week_and_week_are_local) {
  // Fixed 2026-11-14: weekday and week come from the local date, like night mode and the feed date.
  // (Before, they were UTC-based: in CET the configured weekday ran 01:00 -> 01:00 local.)
  ControllerFixture f;
  f.set_time(2026, 11, 14, 23, 59, 0, 3600);  // local Saturday 23:59 CET (22:59 UTC)
  CHECK_EQ(f.ctrl.getCurrentDayOfWeek(), 6);
  f.set_time(2026, 11, 15, 0, 30, 0, 3600);   // local Sunday 00:30 CET (UTC still Saturday)
  CHECK_EQ(f.ctrl.getCurrentDayOfWeek(), 0);
  f.set_time(2026, 11, 19, 0, 30, 0, 7200);   // local Thursday 00:30 (UTC still Wednesday)
  CHECK_EQ(f.ctrl.getCurrentWeekNumber(), 2968);
  f.set_time(2026, 11, 18, 23, 30, 0, 7200);
  CHECK_EQ(f.ctrl.getCurrentWeekNumber(), 2967);
}

TEST(auto_reservoir, triggers_on_configured_weekday_once_per_week) {
  ControllerFixture f;
  arm_auto_reservoir(f);
  CHECK(f.ctrl.shouldTriggerAutoReservoirChange());
  f.ctrl.markAutoResWeek(f.ctrl.getCurrentWeekNumber());  // what handleIdle stores
  CHECK(!f.ctrl.shouldTriggerAutoReservoirChange());
  f.set_time(2026, 11, 21, 18, 0);  // next Saturday
  CHECK(f.ctrl.shouldTriggerAutoReservoirChange());
}

TEST(auto_reservoir, idle_handler_starts_change_and_the_week_survives_a_reboot) {
  ControllerFixture f;
  arm_auto_reservoir(f);
  f.ctrl.auto_feeding_enabled_ = false;
  f.ctrl.auto_ph_correction_enabled_ = false;
  f.hal.level_empty = true;  // something to drain
  f.ctrl.handleIdle();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::WATER_EMPTYING);
  CHECK_EQ(f.ctrl.last_auto_res_week_, 2967);
  CHECK_EQ(std::string(f.psm.getLastEvent().eventID), std::string("RESERVOIR_CHANGE"));

  ControllerFixture after_reboot;
  arm_auto_reservoir(after_reboot);
  CHECK(!after_reboot.ctrl.shouldTriggerAutoReservoirChange());
}

TEST(auto_reservoir, each_condition_blocks) {
  {
    ControllerFixture f;
    arm_auto_reservoir(f);
    f.ctrl.auto_reservoir_change_enabled_ = false;
    CHECK(!f.ctrl.shouldTriggerAutoReservoirChange());
  }
  {
    ControllerFixture f;
    arm_auto_reservoir(f);
    f.force_state(ControllerState::FEEDING);
    CHECK(!f.ctrl.shouldTriggerAutoReservoirChange());
  }
  {
    ControllerFixture f;
    arm_auto_reservoir(f);
    f.set_time(2026, 11, 14, 12, 0);  // light off = night
    CHECK(!f.ctrl.shouldTriggerAutoReservoirChange());
    f.ctrl.setNightModeConfig(false, 22, 9);
    CHECK(f.ctrl.shouldTriggerAutoReservoirChange());
  }
  {
    ControllerFixture f;
    arm_auto_reservoir(f);
    f.ctrl.auto_reservoir_change_day_ = 5;  // Friday
    CHECK(!f.ctrl.shouldTriggerAutoReservoirChange());
  }
  {
    ControllerFixture f;
    arm_auto_reservoir(f);
    f.clock.set_invalid();  // no valid local time
    CHECK(!f.ctrl.shouldTriggerAutoReservoirChange());
  }
  {
    ControllerFixture f;
    arm_auto_reservoir(f);
    f.psm.saveInt32("AutoResWeek", 2967);  // already done this week (stored before a reboot)
    CHECK(!f.ctrl.shouldTriggerAutoReservoirChange());
    CHECK_EQ(f.ctrl.last_auto_res_week_, 2967);
  }
}

TEST(auto_reservoir, default_is_disabled_on_sunday_and_day_setter_clamps) {
  ControllerFixture f;
  CHECK(!f.ctrl.isAutoReservoirChangeEnabled());
  CHECK_EQ(f.ctrl.auto_reservoir_change_day_, 0);
  f.ctrl.setAutoReservoirChangeDay(3);
  CHECK_EQ(f.ctrl.auto_reservoir_change_day_, 3);
  f.ctrl.setAutoReservoirChangeDay(9);
  CHECK_EQ(f.ctrl.auto_reservoir_change_day_, 6);
}

// ---------------------------------------------------------------------------
// handleIdle: periodic pH check slots (aligned to local midnight) and calendar date sync
// ---------------------------------------------------------------------------

// IDLE with every other automatic trigger switched off.
static void quiet_idle(ControllerFixture &f) {
  f.force_state(ControllerState::IDLE);
  f.ctrl.auto_feeding_enabled_ = false;
  f.ctrl.setNightModeConfig(false, 22, 9);
  f.hal.ph_interval_ms = 2 * 3600 * 1000;
}

TEST(idle_ph_schedule, triggers_once_per_two_hour_slot) {
  ControllerFixture f;
  quiet_idle(f);
  f.set_time(2026, 11, 14, 9, 59, 0, 3600);
  f.ctrl.last_ph_check_timestamp_ = f.hal.timestamp - 60;  // checked at 09:58, slot 08:00
  f.ctrl.handleIdle();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);

  f.set_time(2026, 11, 14, 10, 0, 0, 3600);  // slot 10:00 begins
  f.ctrl.handleIdle();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::PH_PROCESSING);
  CHECK_EQ(f.ctrl.last_ph_check_timestamp_, f.hal.timestamp);

  f.force_state(ControllerState::IDLE);
  f.set_time(2026, 11, 14, 11, 59, 59, 3600);  // same slot
  f.ctrl.handleIdle();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);

  f.set_time(2026, 11, 14, 12, 0, 1, 3600);
  f.ctrl.handleIdle();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::PH_PROCESSING);
}

TEST(idle_ph_schedule, first_idle_after_boot_without_setup_triggers_immediately) {
  ControllerFixture f;
  quiet_idle(f);
  f.set_time(2026, 11, 14, 9, 17, 0, 3600);
  CHECK_EQ(f.ctrl.last_ph_check_timestamp_, 0);
  f.ctrl.handleIdle();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::PH_PROCESSING);
}

TEST(idle_ph_schedule, slot_is_consumed_when_auto_ph_is_off_or_night) {
  ControllerFixture f;
  quiet_idle(f);
  f.ctrl.auto_ph_correction_enabled_ = false;
  f.set_time(2026, 11, 14, 10, 0, 0, 3600);
  f.ctrl.last_ph_check_timestamp_ = f.hal.timestamp - 3600;
  f.ctrl.handleIdle();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  CHECK_EQ(f.ctrl.last_ph_check_timestamp_, f.hal.timestamp);  // skipped, not postponed
  // Enabling it later in the same slot does not trigger a check.
  f.ctrl.auto_ph_correction_enabled_ = true;
  f.set_time(2026, 11, 14, 10, 30, 0, 3600);
  f.ctrl.handleIdle();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
}

TEST(idle_ph_schedule, millis_fallback_without_clock) {
  ControllerFixture f;
  quiet_idle(f);
  f.ctrl.last_ph_check_time_ = 0;
  host::set_millis(7199999);
  f.ctrl.handleIdle();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  host::set_millis(7200000);
  f.ctrl.handleIdle();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::PH_PROCESSING);
}

TEST(idle_date_sync, advances_calendar_day_at_local_midnight) {
  ControllerFixture f;
  quiet_idle(f);
  f.ctrl.auto_ph_correction_enabled_ = false;
  f.cal.set_current_day(10);

  f.set_time(2026, 11, 14, 23, 58, 0, 3600);
  f.ctrl.handleIdle();  // first sync seeds the date
  CHECK(f.ctrl.did_initial_date_sync_);
  CHECK_EQ(f.cal.get_current_day(), 10);
  CHECK_EQ(synced_date(f.cal), 20771);

  host::advance_millis(59999);
  f.set_time(2026, 11, 15, 0, 0, 30, 3600);
  f.ctrl.handleIdle();  // re-checked only every 60 s
  CHECK_EQ(f.cal.get_current_day(), 10);

  host::advance_millis(1);
  f.ctrl.handleIdle();
  CHECK_EQ(f.cal.get_current_day(), 11);
  CHECK_EQ(synced_date(f.cal), 20772);
}

TEST(idle_night, enters_night_state_in_night_hours) {
  ControllerFixture f;
  quiet_idle(f);
  f.ctrl.auto_ph_correction_enabled_ = false;
  f.ctrl.setNightModeConfig(true, 22, 9);
  f.set_time(2026, 11, 14, 12, 0);  // day 1 light is off 08:00-16:00
  f.ctrl.handleIdle();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::NIGHT);
}
