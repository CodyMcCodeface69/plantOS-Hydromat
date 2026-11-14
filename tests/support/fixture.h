// Shared fixtures: the real PlantOSController wired to a FakeHAL, the real
// ActuatorSafetyGate, the real PersistentStateManager (on the in-memory NVS),
// the real CalendarManager (loaded with the schedule extracted from plantOS.yaml)
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

// The schedule_json string from plantOS.yaml (extracted at build time, see Makefile).
const std::string &production_schedule_json();

// CalendarManager with the production schedule, set up against the fake NVS.
struct CalendarFixture {
  esphome::calendar_manager::CalendarManager cal;
  explicit CalendarFixture(const std::string &json = production_schedule_json()) {
    cal.set_schedule_json(json);
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
      cal.set_schedule_json(production_schedule_json());
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
