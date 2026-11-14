// Water operations: WATER_FILLING, WATER_EMPTYING, FEED_FILLING driven tick by tick.
// Real code under test: handleWaterFilling, handleWaterEmptying, handleFeedFilling (water_ops.cpp).
#include "minitest.h"
#include "fixture.h"

using plantos_controller::ControllerState;

static bool has_active_alert(ControllerFixture &f, const std::string &type) {
  for (auto &a : f.ctrl.status_logger_.getActiveAlerts())
    if (a.type == type) return true;
  return false;
}

static void tick(ControllerFixture &f, ControllerState s, uint32_t ms = 100) {
  host::advance_millis(ms);
  switch (s) {
    case ControllerState::WATER_FILLING: f.ctrl.handleWaterFilling(); break;
    case ControllerState::WATER_EMPTYING: f.ctrl.handleWaterEmptying(); break;
    case ControllerState::FEED_FILLING: f.ctrl.handleFeedFilling(); break;
    default: break;
  }
}

// Runs the handler of state s every 100 ms until the state changes or max_ms passed.
static void run_while(ControllerFixture &f, ControllerState s, uint32_t max_ms) {
  uint32_t start = esphome::millis();
  while (f.ctrl.getCurrentState() == s && esphome::millis() - start < max_ms) tick(f, s);
}

static void enter(ControllerFixture &f, ControllerState s) {
  host::set_millis(1000);
  f.force_state(s);
  switch (s) {
    case ControllerState::WATER_FILLING: f.ctrl.handleWaterFilling(); break;
    case ControllerState::WATER_EMPTYING: f.ctrl.handleWaterEmptying(); break;
    case ControllerState::FEED_FILLING: f.ctrl.handleFeedFilling(); break;
    default: break;
  }
}

static bool event_cleared(ControllerFixture &f) {
  return !f.psm.hasEvent() || std::string(f.psm.getLastEvent().eventID).empty();
}

// ---------------------------------------------------------------------------
// WATER_FILLING
// ---------------------------------------------------------------------------

TEST(water_filling, opens_valve_and_closes_on_high_then_queues_ec_and_ph) {
  ControllerFixture f;
  f.psm.logEvent("WATER_FILL", 0);
  f.ctrl.auto_ph_correction_pending_ = true;
  f.ctrl.ec_attempt_count_ = 2;
  enter(f, ControllerState::WATER_FILLING);
  CHECK(f.hal.getValveState("WaterValve"));
  CHECK(f.gate.getState("WaterValve"));
  run_while(f, ControllerState::WATER_FILLING, 5000);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::WATER_FILLING);  // still filling
  f.hal.level_low = true;
  tick(f, ControllerState::WATER_FILLING);
  f.hal.level_high = true;
  tick(f, ControllerState::WATER_FILLING);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  CHECK(!f.hal.getValveState("WaterValve"));
  CHECK(f.ctrl.auto_ec_check_pending_);
  CHECK(f.ctrl.auto_ph_correction_pending_);
  CHECK(f.ctrl.ph_post_fill_stabilize_);
  CHECK_EQ((int)f.ctrl.ec_attempt_count_, 0);
  CHECK(event_cleared(f));
}

TEST(water_filling, timeout_after_600_s_closes_and_still_queues_ec_and_ph) {
  ControllerFixture f;
  enter(f, ControllerState::WATER_FILLING);
  run_while(f, ControllerState::WATER_FILLING, 599800);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::WATER_FILLING);
  run_while(f, ControllerState::WATER_FILLING, 1000);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  CHECK_EQ(esphome::millis(), 601000u);  // elapsed 600 s exactly
  CHECK(!f.hal.getValveState("WaterValve"));
  CHECK(has_active_alert(f, "HARDWARE_WATER_SENSOR_HIGH"));
  CHECK(f.ctrl.auto_ec_check_pending_);
  CHECK(f.ctrl.ph_post_fill_stabilize_);
}

TEST(water_filling, sensors_lost_mid_fill_close_valve_and_abort) {
  ControllerFixture f;
  f.ctrl.auto_ph_correction_pending_ = true;
  enter(f, ControllerState::WATER_FILLING);
  tick(f, ControllerState::WATER_FILLING);
  f.hal.has_level_sensors = false;
  tick(f, ControllerState::WATER_FILLING);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  CHECK(!f.hal.getValveState("WaterValve"));
  CHECK(!f.ctrl.auto_ph_correction_pending_);
  CHECK(!f.ctrl.auto_ec_check_pending_);
}

TEST(water_filling, no_sensors_at_entry_refuses_with_alert) {
  ControllerFixture f;
  f.ctrl.auto_ph_correction_pending_ = true;
  f.hal.has_level_sensors = false;
  enter(f, ControllerState::WATER_FILLING);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  CHECK(!f.hal.getValveState("WaterValve"));
  CHECK(has_active_alert(f, "HARDWARE_WATER_SENSORS_MISSING"));
  CHECK(!f.ctrl.auto_ph_correction_pending_);
}

TEST(water_filling, gate_rejection_aborts_with_alert) {
  ControllerFixture f;
  f.gate.setMaxDuration("WaterValve", 100);  // 600 s request > 100 s max
  enter(f, ControllerState::WATER_FILLING);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  CHECK(!f.hal.getValveState("WaterValve"));
  CHECK(has_active_alert(f, "PUMP_REJECTION_WATER_VALVE"));
  // the next fill opens the valve again
  f.gate.setMaxDuration("WaterValve", 0);
  enter(f, ControllerState::WATER_FILLING);
  CHECK(f.hal.getValveState("WaterValve"));
  f.hal.level_high = true;
  tick(f, ControllerState::WATER_FILLING);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  CHECK(!has_active_alert(f, "PUMP_REJECTION_WATER_VALVE"));
}

// ---------------------------------------------------------------------------
// WATER_EMPTYING
// ---------------------------------------------------------------------------

TEST(water_emptying, drains_until_empty_clears) {
  for (bool pending : {false, true}) {
    ControllerFixture f;
    f.hal.level_empty = true;
    f.ctrl.auto_ph_correction_pending_ = pending;
    enter(f, ControllerState::WATER_EMPTYING);
    CHECK(f.hal.getPumpState("WastewaterPump"));
    run_while(f, ControllerState::WATER_EMPTYING, 10000);
    CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::WATER_EMPTYING);
    f.hal.level_empty = false;
    tick(f, ControllerState::WATER_EMPTYING);
    CHECK_EQ(f.ctrl.getCurrentState(), pending ? ControllerState::FEED_FILLING : ControllerState::IDLE);
    CHECK(!f.hal.getPumpState("WastewaterPump"));
    CHECK(event_cleared(f));
  }
}

TEST(water_emptying, already_empty_at_entry) {
  for (bool pending : {false, true}) {
    ControllerFixture f;
    f.hal.level_empty = false;
    f.ctrl.auto_ph_correction_pending_ = pending;
    enter(f, ControllerState::WATER_EMPTYING);
    CHECK_EQ(f.ctrl.getCurrentState(), pending ? ControllerState::FEED_FILLING : ControllerState::IDLE);
    CHECK(!f.hal.getPumpState("WastewaterPump"));
    CHECK_EQ(f.hal.count("pump", "WastewaterPump", true), 0);
  }
}

TEST(water_emptying, timeout_after_300_s) {
  for (bool sensors : {true, false}) {
    for (bool pending : {false, true}) {
      ControllerFixture f;
      f.hal.level_empty = true;
      f.hal.has_level_sensors = sensors;
      f.ctrl.auto_ph_correction_pending_ = pending;
      enter(f, ControllerState::WATER_EMPTYING);
      run_while(f, ControllerState::WATER_EMPTYING, 299800);
      CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::WATER_EMPTYING);
      run_while(f, ControllerState::WATER_EMPTYING, 1000);
      CHECK_EQ(esphome::millis(), 301000u);
      CHECK_EQ(f.ctrl.getCurrentState(), pending ? ControllerState::FEED_FILLING : ControllerState::IDLE);
      CHECK(!f.hal.getPumpState("WastewaterPump"));
      CHECK_EQ(has_active_alert(f, "HARDWARE_WATER_SENSOR_EMPTY"), sensors);
    }
  }
}

TEST(water_emptying, shelly_disabled_mid_drain_aborts) {
  ControllerFixture f;
  f.hal.level_empty = true;
  f.ctrl.auto_ph_correction_pending_ = true;
  enter(f, ControllerState::WATER_EMPTYING);
  tick(f, ControllerState::WATER_EMPTYING);
  f.hal.shelly_enabled = false;
  tick(f, ControllerState::WATER_EMPTYING);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  CHECK(!f.hal.getPumpState("WastewaterPump"));
  CHECK(!f.ctrl.auto_ph_correction_pending_);
  // next drain starts the pump again
  f.hal.shelly_enabled = true;
  enter(f, ControllerState::WATER_EMPTYING);
  CHECK(f.hal.getPumpState("WastewaterPump"));
  f.hal.level_empty = false;
  tick(f, ControllerState::WATER_EMPTYING);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
}

TEST(water_emptying, gate_rejection_aborts_reservoir_change) {
  ControllerFixture f;
  f.hal.level_empty = true;
  f.ctrl.auto_ph_correction_pending_ = true;
  f.gate.setMaxDuration("WastewaterPump", 100);  // 300 s request > 100 s max
  enter(f, ControllerState::WATER_EMPTYING);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  CHECK(!f.ctrl.auto_ph_correction_pending_);
  CHECK(has_active_alert(f, "PUMP_REJECTION_WASTEWATER"));
}

// ---------------------------------------------------------------------------
// FEED_FILLING
// ---------------------------------------------------------------------------

TEST(feed_filling, fills_until_high_and_low_then_feeds) {
  ControllerFixture f;
  f.ctrl.auto_ph_correction_pending_ = true;
  enter(f, ControllerState::FEED_FILLING);
  CHECK(f.hal.getValveState("WaterValve"));
  run_while(f, ControllerState::FEED_FILLING, 5000);
  f.hal.level_empty = true;
  f.hal.level_high = true;  // HIGH alone is not enough
  tick(f, ControllerState::FEED_FILLING);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::FEED_FILLING);
  f.hal.level_low = true;
  tick(f, ControllerState::FEED_FILLING);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::FEEDING);
  CHECK(!f.hal.getValveState("WaterValve"));
  CHECK(f.ctrl.auto_ph_correction_pending_);
}

TEST(feed_filling, timeout_from_volume_and_flow_then_feeds_anyway) {
  // delta volume 27.4 L, valve 100 mL/s: 274 s x 1.2 = 328.8 s
  // reservoir change total volume 47.5 L: 475 s x 1.2 = 570 s
  for (bool reservoir : {false, true}) {
    ControllerFixture f;
    f.ctrl.is_reservoir_change_ = reservoir;
    enter(f, ControllerState::FEED_FILLING);
    uint32_t limit_ms = reservoir ? 570000 : 328800;
    run_while(f, ControllerState::FEED_FILLING, limit_ms - 200);
    CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::FEED_FILLING);
    run_while(f, ControllerState::FEED_FILLING, 1000);
    CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::FEEDING);
    CHECK_EQ(esphome::millis(), 1000u + limit_ms);
    CHECK(!f.hal.getValveState("WaterValve"));
  }
}

TEST(feed_filling, refuses_tank_that_is_not_empty_or_without_sensors) {
  {
    ControllerFixture f;
    f.ctrl.auto_ph_correction_pending_ = true;
    f.hal.level_empty = true;
    enter(f, ControllerState::FEED_FILLING);
    CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
    CHECK(!f.ctrl.auto_ph_correction_pending_);
    CHECK_EQ(f.hal.count("valve", "WaterValve", true), 0);
  }
  {
    ControllerFixture f;
    f.ctrl.auto_ph_correction_pending_ = true;
    f.hal.has_level_sensors = false;
    enter(f, ControllerState::FEED_FILLING);
    CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
    CHECK(!f.ctrl.auto_ph_correction_pending_);
    CHECK(has_active_alert(f, "HARDWARE_WATER_SENSORS_MISSING"));
  }
  {
    ControllerFixture f;
    f.ctrl.auto_ph_correction_pending_ = true;
    f.gate.setMaxDuration("WaterValve", 100);  // 328 s request > 100 s max
    enter(f, ControllerState::FEED_FILLING);
    CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
    CHECK(!f.ctrl.auto_ph_correction_pending_);
  }
}

// ---------------------------------------------------------------------------
// Entry action: runs once per entry, independent of when the first tick comes
// ---------------------------------------------------------------------------

TEST(water_filling, first_tick_late_after_transition_still_opens_valve) {
  ControllerFixture f;
  host::set_millis(1000);
  f.force_state(ControllerState::IDLE);
  f.ctrl.startFillTank();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::WATER_FILLING);
  tick(f, ControllerState::WATER_FILLING, 250);  // first handler call 250 ms after the transition
  CHECK(f.hal.getValveState("WaterValve"));
  f.hal.level_high = true;
  tick(f, ControllerState::WATER_FILLING);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  CHECK(!has_active_alert(f, "HARDWARE_WATER_SENSOR_HIGH"));
}

TEST(water_filling, fill_cut_by_emergency_stop_opens_again_next_time) {
  ControllerFixture f;
  host::set_millis(1000);
  f.force_state(ControllerState::IDLE);
  f.ctrl.startFillTank();
  tick(f, ControllerState::WATER_FILLING, 10);
  CHECK(f.hal.getValveState("WaterValve"));
  f.ctrl.emergencyStop();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::SHUTDOWN);
  CHECK(!f.hal.getValveState("WaterValve"));
  host::advance_millis(5000);
  f.ctrl.setToIdle();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  host::advance_millis(5000);
  f.ctrl.startFillTank();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::WATER_FILLING);
  tick(f, ControllerState::WATER_FILLING, 10);
  CHECK(f.hal.getValveState("WaterValve"));
}

TEST(water_emptying, first_tick_late_after_transition_still_starts_pump) {
  ControllerFixture f;
  f.hal.level_empty = true;
  host::set_millis(1000);
  f.force_state(ControllerState::IDLE);
  f.ctrl.startEmptyTank();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::WATER_EMPTYING);
  tick(f, ControllerState::WATER_EMPTYING, 250);
  CHECK(f.hal.getPumpState("WastewaterPump"));
}

TEST(water_emptying, drain_cut_by_pause_starts_pump_again_next_time) {
  ControllerFixture f;
  f.hal.level_empty = true;
  host::set_millis(1000);
  f.force_state(ControllerState::IDLE);
  f.ctrl.startEmptyTank();
  tick(f, ControllerState::WATER_EMPTYING, 10);
  CHECK(f.hal.getPumpState("WastewaterPump"));
  f.ctrl.setToPause();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::PAUSE);
  f.ctrl.turnOffAllPumps();  // what PAUSE does to the actuators
  CHECK(!f.hal.getPumpState("WastewaterPump"));
  host::advance_millis(5000);
  f.ctrl.setToIdle();
  host::advance_millis(5000);
  f.ctrl.startEmptyTank();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::WATER_EMPTYING);
  tick(f, ControllerState::WATER_EMPTYING, 10);
  CHECK(f.hal.getPumpState("WastewaterPump"));
}
