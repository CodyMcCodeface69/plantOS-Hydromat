// Shared fixtures: the real PlantOSController wired to a FakeHAL, the real
// ActuatorSafetyGate, the real PersistentStateManager (on the in-memory NVS),
// the real CalendarManager (with the phase tables generated from plantOS.yaml)
// and a fake RealTimeClock.
//
// The test binary is compiled with -fno-access-control, so tests may call
// private methods and read/write private members of the real classes.
#pragma once
#include <string>
#include "fake_hal.h"
#include "esphome/components/actuator_safety_gate/ActuatorSafetyGate.h"
#include "esphome/components/alert_service/alert_service.h"
#include "esphome/components/calendar_manager/CalendarManager.h"
#include "esphome/components/persistent_state_manager/persistent_state_manager.h"
#include "esphome/components/plantos_controller/controller.h"
#include "esphome/components/time/real_time_clock.h"

// The three calendars from plantOS.yaml (generated at build time by the firmware's own
// phase_tables.py, see Makefile), indexed by calendar_manager::CalendarIndex.
const esphome::calendar_manager::CalendarDef *production_calendars();

// A phase with the values the old JSON parser used for missing fields.
inline esphome::calendar_manager::Phase test_phase() {
  return {1, 5.8f, 6.2f, 0.0f, 0.0f, 0.0f, 960, 480, 0.0f, 0.2f, "test"};
}

// Three calendars that are all one endless phase `p` (synthetic calendar tests).
struct OnePhaseCalendars {
  esphome::calendar_manager::Phase phase;
  esphome::calendar_manager::CalendarDef defs[3];
  explicit OnePhaseCalendars(const esphome::calendar_manager::Phase &p)
      : phase(p), defs{{"A", &phase, 1, 0}, {"M", &phase, 1, 0}, {"H", &phase, 1, 0}} {}
  OnePhaseCalendars(const OnePhaseCalendars &) = delete;
};

// The active calendar's last synced date (private slot state).
inline int32_t synced_date(const esphome::calendar_manager::CalendarManager &c) {
  return c.slots_[c.get_active_calendar()].last_sync_date;
}

// CalendarManager with the production calendars, set up against the fake NVS.
struct CalendarFixture {
  esphome::calendar_manager::CalendarManager cal;
  explicit CalendarFixture(const esphome::calendar_manager::CalendarDef *cals = production_calendars()) {
    cal.set_calendars(cals);
    cal.setup();
  }
};

struct ControllerFixture {
  FakeHAL hal;
  esphome::actuator_safety_gate::ActuatorSafetyGate gate;
  esphome::persistent_state_manager::PersistentStateManager psm;
  esphome::calendar_manager::CalendarManager cal;
  esphome::time::RealTimeClock clock;
  plantos_controller::PlantOSController ctrl;

  // with_calendar=false leaves calendar_manager_ null (the controller's fallback paths).
  explicit ControllerFixture(bool with_calendar = true) {
    gate.setHAL(&hal);
    psm.setup();
    ctrl.setHAL(&hal);
    ctrl.setSafetyGate(&gate);
    ctrl.setPersistenceManager(&psm);
    ctrl.setTimeSource(&clock);
    if (with_calendar) {
      cal.set_calendars(production_calendars());
      cal.setup();
      ctrl.setCalendarManager(&cal);
    }
  }

  // Put the FSM into a state without running transition side effects.
  void force_state(plantos_controller::ControllerState s) {
    ctrl.current_state_ = s;
    ctrl.state_start_time_ = esphome::millis();
    ctrl.state_entry_executed_ = false;
  }

  // Set the wall clock on both time paths the controller uses (RealTimeClock and HAL).
  // y/mo/d h:mi:s is LOCAL time; utc_offset_s says how far it is ahead of UTC (CET = 3600).
  void set_time(int y, int mo, int d, int h, int mi, int s = 0, int utc_offset_s = 0) {
    clock.set_local(y, mo, d, h, mi, s, utc_offset_s);
    hal.timestamp = clock.now().timestamp;
    hal.seconds_since_midnight = h * 3600 + mi * 60 + s;
    hal.has_time = true;
    host::set_unix_time(hal.timestamp);
  }
};
