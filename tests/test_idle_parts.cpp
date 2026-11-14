// Parts of handleIdle / handleNight / loop that the split into named methods touches:
// PSM hold check (every 5 s), grow light schedule.
#include "minitest.h"
#include "fixture.h"

using plantos_controller::ControllerState;

TEST(idle_psm_hold, pause_or_shutdown_in_psm_leaves_idle_and_night_every_5_s) {
  for (ControllerState from : {ControllerState::IDLE, ControllerState::NIGHT}) {
    for (const char *ev : {"STATE_PAUSE", "STATE_SHUTDOWN"}) {
      ControllerFixture f;
      f.set_time(2026, 11, 14, from == ControllerState::NIGHT ? 12 : 18, 0);  // day 1: 08-16 dark
      f.ctrl.auto_ph_correction_enabled_ = false;
      f.ctrl.auto_feeding_enabled_ = false;
      host::set_millis(4000);
      f.force_state(from);
      f.psm.logEvent(ev, 1);
      if (from == ControllerState::IDLE) f.ctrl.handleIdle(); else f.ctrl.handleNight();
      CHECK_EQ(f.ctrl.getCurrentState(), from);  // 4000 - 0 < 5000
      host::set_millis(5000);
      if (from == ControllerState::IDLE) f.ctrl.handleIdle(); else f.ctrl.handleNight();
      CHECK_EQ(f.ctrl.getCurrentState(),
               std::string(ev) == "STATE_PAUSE" ? ControllerState::PAUSE : ControllerState::SHUTDOWN);
    }
  }
}

TEST(idle_psm_hold, other_events_do_not_hold) {
  ControllerFixture f;
  f.set_time(2026, 11, 14, 18, 0);
  f.ctrl.auto_ph_correction_enabled_ = false;
  f.ctrl.auto_feeding_enabled_ = false;
  host::set_millis(10000);
  f.force_state(ControllerState::IDLE);
  f.psm.logEvent("FEED_OPERATION", 0);
  f.ctrl.handleIdle();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
}

TEST(grow_light, schedule_switches_once_per_change_when_enabled) {
  ControllerFixture f;
  f.hal.has_level_sensors = false;
  f.ctrl.auto_ph_correction_enabled_ = false;
  f.ctrl.auto_feeding_enabled_ = false;
  f.force_state(ControllerState::PAUSE);  // keep handlers quiet; PAUSE also blocks the light
  f.set_time(2026, 11, 14, 18, 0);        // day 1: light 16:00-08:00 -> on
  f.ctrl.controlGrowLight();
  CHECK_EQ(f.hal.count("pump", "GrowLight", true), 0);
  f.force_state(ControllerState::SHUTDOWN);
  f.ctrl.state_entry_executed_ = true;
  f.ctrl.controlGrowLight();
  CHECK_EQ(f.hal.count("pump", "GrowLight", true), 0);

  f.force_state(ControllerState::FEEDING);
  f.ctrl.controlGrowLight();  // schedule disabled (default)
  CHECK_EQ(f.hal.count("pump", "GrowLight", true), 0);
  f.ctrl.setGrowLightScheduleEnabled(true);
  f.ctrl.controlGrowLight();
  f.ctrl.controlGrowLight();
  CHECK_EQ(f.hal.count("pump", "GrowLight", true), 1);
  f.set_time(2026, 11, 14, 9, 0);  // dark
  host::advance_millis(60000);
  f.ctrl.controlGrowLight();
  f.ctrl.controlGrowLight();
  CHECK_EQ(f.hal.count("pump", "GrowLight", false), 1);
}
