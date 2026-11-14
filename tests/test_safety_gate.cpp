// ActuatorSafetyGate: debouncing, max durations, auto-shutoff, interlock, ramping, cycling.
// Real code under test: components/actuator_safety_gate/ActuatorSafetyGate.{h,cpp}
#include "minitest.h"
#include "fake_hal.h"
#include "esphome/components/actuator_safety_gate/ActuatorSafetyGate.h"

using namespace esphome::actuator_safety_gate;

struct GateFixture {
  FakeHAL hal;
  ActuatorSafetyGate gate;
  GateFixture() {
    gate.setHAL(&hal);
    host::set_millis(1000);  // turnOnTime == 0 means "off" in the gate, so do not start at 0
  }
};

// ---------------------------------------------------------------------------
// Debouncing
// ---------------------------------------------------------------------------

TEST(gate_debounce, same_state_is_rejected) {
  GateFixture f;
  CHECK(!f.gate.executeCommand("AcidPump", false));  // unknown actuator starts OFF
  CHECK(f.gate.executeCommand("AcidPump", true, 5));
  CHECK(!f.gate.executeCommand("AcidPump", true, 5));
  CHECK(f.gate.executeCommand("AcidPump", false));
  CHECK(!f.gate.executeCommand("AcidPump", false));
  CHECK_EQ(f.hal.count("pump", "AcidPump", true), 1);
  CHECK_EQ(f.hal.count("pump", "AcidPump", false), 1);
}

TEST(gate_debounce, there_is_no_time_based_lockout) {
  GateFixture f;
  for (int i = 0; i < 5; i++) {
    CHECK(f.gate.executeCommand("AcidPump", true, 5));
    CHECK(f.gate.executeCommand("AcidPump", false));  // same millisecond
  }
  CHECK_EQ(f.hal.count("pump", "AcidPump", true), 5);
}

TEST(gate_debounce, force_execute_and_disabled_gate_bypass_it) {
  GateFixture f;
  CHECK(f.gate.executeCommand("AirPump", true));
  CHECK(f.gate.executeCommand("AirPump", true, 0, true));  // forceExecute
  CHECK_EQ(f.hal.count("pump", "AirPump", true), 2);
  f.gate.setEnabled(false);
  CHECK(f.gate.executeCommand("AirPump", true));
  CHECK_EQ(f.hal.count("pump", "AirPump", true), 3);
}

TEST(gate_debounce, invalid_ids_are_rejected) {
  GateFixture f;
  CHECK(!f.gate.executeCommand(nullptr, true));
  CHECK(!f.gate.executeCommand("", true));
  CHECK_EQ(f.hal.calls.size(), 0u);
}

// ---------------------------------------------------------------------------
// Max duration
// ---------------------------------------------------------------------------

TEST(gate_duration, request_above_limit_is_rejected) {
  GateFixture f;
  f.gate.setMaxDuration("AcidPump", 30);
  CHECK_EQ(f.gate.getMaxDurationSeconds("AcidPump"), 30u);
  CHECK(!f.gate.executeCommand("AcidPump", true, 31));
  CHECK(!f.gate.getState("AcidPump"));
  CHECK(f.gate.executeCommand("AcidPump", true, 30));
  CHECK(f.gate.getState("AcidPump"));
  CHECK_EQ(f.hal.count("pump", "AcidPump", true), 1);
}

TEST(gate_duration, on_without_duration_is_accepted_despite_limit) {
  // maxDurationSeconds == 0 skips the request check; the loop() watchdog still applies.
  GateFixture f;
  f.gate.setMaxDuration("AcidPump", 30);
  CHECK(f.gate.executeCommand("AcidPump", true, 0));
  CHECK(f.gate.getState("AcidPump"));
}

TEST(gate_duration, loop_forces_off_after_configured_maximum_not_requested_duration) {
  GateFixture f;
  f.gate.setMaxDuration("AcidPump", 30);
  CHECK(f.gate.executeCommand("AcidPump", true, 5));  // caller asked for 5 s
  host::advance_millis(6000);
  f.gate.loop();
  CHECK(f.gate.getState("AcidPump"));  // the gate does not time the requested 5 s
  CHECK_EQ(f.gate.getRuntime("AcidPump"), 6u);
  host::advance_millis(24000);  // exactly 30 s
  f.gate.loop();
  CHECK(f.gate.getState("AcidPump"));
  CHECK(!f.gate.isViolatingDuration("AcidPump"));
  host::advance_millis(1);
  CHECK(f.gate.isViolatingDuration("AcidPump"));
  f.gate.loop();
  CHECK(!f.gate.getState("AcidPump"));
  CHECK_EQ(f.hal.count("pump", "AcidPump", false), 1);
  CHECK_EQ(f.gate.getRuntime("AcidPump"), 0u);
  // After the forced shutoff the pump can be started again.
  CHECK(f.gate.executeCommand("AcidPump", true, 5));
}

TEST(gate_duration, no_limit_means_no_auto_shutoff) {
  GateFixture f;
  CHECK(f.gate.executeCommand("AirPump", true));
  host::advance_millis(24u * 3600u * 1000u);
  f.gate.loop();
  CHECK(f.gate.getState("AirPump"));
}

TEST(gate_duration, disabled_gate_skips_limit_and_watchdog) {
  GateFixture f;
  f.gate.setMaxDuration("AcidPump", 30);
  f.gate.setEnabled(false);
  CHECK(f.gate.executeCommand("AcidPump", true, 100));
  host::advance_millis(60000);
  f.gate.loop();
  CHECK(f.gate.getState("AcidPump"));
  f.gate.setEnabled(true);  // re-enabling catches the overrun on the next loop
  f.gate.loop();
  CHECK(!f.gate.getState("AcidPump"));
}

TEST(gate_duration, adapted_duration_clamps_to_limit) {
  GateFixture f;
  CHECK_EQ(f.gate.getAdaptedDuration("AcidPump", 99), 99u);  // unknown actuator: no limit
  f.gate.setMaxDuration("AcidPump", 30);
  CHECK_EQ(f.gate.getAdaptedDuration("AcidPump", 10), 10u);
  CHECK_EQ(f.gate.getAdaptedDuration("AcidPump", 30), 30u);
  CHECK_EQ(f.gate.getAdaptedDuration("AcidPump", 31), 30u);
}

TEST(gate_duration, stats_report_state_runtime_and_limit) {
  GateFixture f;
  bool on = true;
  uint32_t runtime = 99, max = 99;
  CHECK(!f.gate.getStats("AcidPump", on, runtime, max));
  f.gate.setMaxDuration("AcidPump", 30);
  f.gate.executeCommand("AcidPump", true, 10);
  host::advance_millis(4500);
  CHECK(f.gate.getStats("AcidPump", on, runtime, max));
  CHECK(on);
  CHECK_EQ(runtime, 4u);
  CHECK_EQ(max, 30u);
}

// ---------------------------------------------------------------------------
// Water valve interlock
// ---------------------------------------------------------------------------

TEST(gate_interlock, water_valve_refused_and_closed_when_tank_full) {
  GateFixture f;
  f.hal.level_high = true;
  CHECK(!f.gate.executeCommand("WaterValve", true, 10));
  CHECK_EQ(f.hal.calls.size(), 0u);

  f.hal.level_high = false;
  CHECK(f.gate.executeCommand("WaterValve", true, 10));
  CHECK_EQ(f.hal.count("valve", "WaterValve", true), 1);
  f.gate.loop();
  CHECK(f.gate.getState("WaterValve"));
  f.hal.level_high = true;  // tank fills up
  f.gate.loop();
  CHECK(!f.gate.getState("WaterValve"));
  CHECK_EQ(f.hal.count("valve", "WaterValve", false), 1);
}

TEST(gate_interlock, other_actuators_ignore_the_level_sensor) {
  GateFixture f;
  f.hal.level_high = true;
  CHECK(f.gate.executeCommand("NutrientPumpA", true, 5));
}

// ---------------------------------------------------------------------------
// HAL routing
// ---------------------------------------------------------------------------

TEST(gate_routing, id_substring_selects_hal_call) {
  GateFixture f;
  f.gate.executeCommand("WaterValve", true);
  f.gate.executeCommand("GrowLight", true);
  f.gate.executeCommand("Mystery", true);
  CHECK_EQ(f.hal.count("valve", "WaterValve", true), 1);
  CHECK_EQ(f.hal.count("pump", "GrowLight", true), 1);  // lights go through setPump
  CHECK_EQ(f.hal.count("pump", "Mystery", true), 1);    // unknown types default to setPump
}

TEST(gate_routing, works_without_hal) {
  ActuatorSafetyGate gate;
  CHECK(gate.executeCommand("AcidPump", true, 5));
  CHECK(gate.getState("AcidPump"));
}

// ---------------------------------------------------------------------------
// Soft start / soft stop
// ---------------------------------------------------------------------------

TEST(gate_ramp, soft_start_delays_hal_on_until_ramp_end) {
  GateFixture f;
  f.gate.setRampDuration("AcidPump", 1000);
  CHECK(f.gate.executeCommand("AcidPump", true, 10));
  CHECK_EQ(f.gate.getRampState("AcidPump"), RAMP_STARTING);
  CHECK(f.gate.isRamping("AcidPump"));
  CHECK_EQ(f.hal.calls.size(), 0u);  // nothing sent to the HAL during the ramp
  host::advance_millis(250);
  f.gate.loop();
  CHECK_NEAR(f.gate.getDutyCycle("AcidPump"), 0.25f, 1e-6);
  host::advance_millis(750);
  f.gate.loop();
  CHECK_EQ(f.gate.getRampState("AcidPump"), RAMP_FULL_ON);
  CHECK_NEAR(f.gate.getDutyCycle("AcidPump"), 1.0f, 1e-6);
  CHECK_EQ(f.hal.count("pump", "AcidPump", true), 1);
}

TEST(gate_ramp, soft_stop_delays_hal_off_until_ramp_end) {
  GateFixture f;
  f.gate.setRampDuration("AcidPump", 1000);
  f.gate.executeCommand("AcidPump", true, 10);
  host::advance_millis(1000);
  f.gate.loop();
  CHECK(f.gate.executeCommand("AcidPump", false));
  CHECK_EQ(f.gate.getRampState("AcidPump"), RAMP_STOPPING);
  CHECK(!f.gate.getState("AcidPump"));  // tracked as OFF immediately
  CHECK_EQ(f.hal.count("pump", "AcidPump", false), 0);
  host::advance_millis(400);
  f.gate.loop();
  CHECK_NEAR(f.gate.getDutyCycle("AcidPump"), 0.6f, 1e-6);
  host::advance_millis(600);
  f.gate.loop();
  CHECK_EQ(f.gate.getRampState("AcidPump"), RAMP_OFF);
  CHECK_EQ(f.hal.count("pump", "AcidPump", false), 1);
}

TEST(gate_ramp, stop_during_soft_start_restarts_ramp_from_full_scale) {
  // The down-ramp always computes 1 - progress, so stopping at 30 % duty jumps to 100 % first.
  GateFixture f;
  f.gate.setRampDuration("AcidPump", 1000);
  f.gate.executeCommand("AcidPump", true, 10);
  host::advance_millis(300);
  f.gate.loop();
  CHECK_NEAR(f.gate.getDutyCycle("AcidPump"), 0.3f, 1e-6);
  f.gate.executeCommand("AcidPump", false);
  host::advance_millis(100);
  f.gate.loop();
  CHECK_NEAR(f.gate.getDutyCycle("AcidPump"), 0.9f, 1e-6);
}

// ---------------------------------------------------------------------------
// State sync, force reset
// ---------------------------------------------------------------------------

TEST(gate_sync, update_actual_state_adopts_external_state) {
  GateFixture f;
  f.gate.updateActualState("AirPump", true);  // unknown actuator gets registered
  CHECK(f.gate.getState("AirPump"));
  CHECK(!f.gate.executeCommand("AirPump", true));  // debounced against the synced state
  host::advance_millis(5000);
  CHECK_EQ(f.gate.getRuntime("AirPump"), 5u);
  f.gate.updateActualState("AirPump", false);
  CHECK(!f.gate.getState("AirPump"));
  CHECK_EQ(f.gate.getRuntime("AirPump"), 0u);
  CHECK_EQ(f.hal.calls.size(), 0u);  // syncing never drives hardware
}

TEST(gate_sync, force_reset_changes_tracking_only) {
  GateFixture f;
  f.gate.executeCommand("AcidPump", true, 5);
  f.gate.forceReset("AcidPump", false);
  CHECK(!f.gate.getState("AcidPump"));
  CHECK_EQ(f.hal.count("pump", "AcidPump", false), 0);  // hardware was not switched off
}

// ---------------------------------------------------------------------------
// Cycling
// ---------------------------------------------------------------------------

TEST(gate_cycling, generic_actuator_toggles_on_and_off_periods) {
  GateFixture f;
  f.gate.setCyclingPeriods("WastewaterPump", 10, 20);
  f.gate.enableCycling("WastewaterPump", true);
  CHECK(f.gate.isCyclingEnabled("WastewaterPump"));
  CHECK(f.gate.getState("WastewaterPump"));  // starts with the ON period
  host::advance_millis(9999);
  f.gate.loop();
  CHECK(f.gate.getState("WastewaterPump"));
  host::advance_millis(1);
  f.gate.loop();
  CHECK(!f.gate.getState("WastewaterPump"));
  host::advance_millis(20000);
  f.gate.loop();
  CHECK(f.gate.getState("WastewaterPump"));
  f.gate.enableCycling("WastewaterPump", false);
  CHECK(!f.gate.getState("WastewaterPump"));
}

TEST(gate_cycling, air_pump_uses_shelly_pattern_of_about_ten_minutes) {
  GateFixture f;
  f.gate.setCyclingPeriods("AirPump", 120, 60);
  f.gate.enableCycling("AirPump", true);
  // 600 s / 180 s = 3 cycles, no trailing OFF: [120, 60, 120, 60, 120]
  std::vector<uint32_t> expected{120, 60, 120, 60, 120};
  CHECK(f.hal.last_pattern == expected);
  CHECK_EQ(f.hal.count("pattern", "AirPump", true), 1);
  CHECK_EQ(f.hal.count("pump", "AirPump", true), 0);  // no direct switching

  host::advance_millis(9 * 60 * 1000);
  f.gate.loop();
  CHECK_EQ(f.hal.count("pattern", "AirPump", true), 1);  // refresh needs MORE than 9 min
  host::advance_millis(1);
  f.gate.loop();
  CHECK_EQ(f.hal.count("pattern", "AirPump", true), 2);
}

TEST(gate_cycling, air_pump_pattern_needs_periods_and_respects_max_duration) {
  GateFixture f;
  f.gate.enableCycling("AirPump", true);  // periods not configured
  CHECK_EQ(f.hal.calls.size(), 0u);

  f.gate.setMaxDuration("AirPump", 100);
  CHECK(f.gate.validateAirPumpPattern({100, 500, 100}));   // only ON entries (even index) count
  CHECK(!f.gate.validateAirPumpPattern({100, 50, 101}));
  CHECK(!f.gate.setAirPumpPattern({101}, true));
  CHECK(!f.gate.setAirPumpPattern({}, true));
  CHECK(f.gate.setAirPumpPattern({100, 10}, false));
}

TEST(gate_cycling, air_pump_normal_mode_waits_for_first_state_poll) {
  GateFixture f;
  f.gate.enableCycling("AirPump", false);
  CHECK_EQ(f.hal.calls.size(), 0u);  // deferred: actual state unknown
  CHECK(!f.gate.stopAirPumpSequence(true));

  f.gate.updateActualState("AirPump", false);  // first poll: pump is off
  f.gate.enableCycling("AirPump", false);
  CHECK_EQ(f.hal.count("stop_sequence", "AirPump", true), 1);
  // SUSPECT (minor): this path does not update the tracked state, so the gate still reports
  // OFF until the next Shelly poll calls updateActualState().
  CHECK(!f.gate.getState("AirPump"));

  CHECK(f.gate.stopAirPumpSequence(true));  // the direct API does update tracking
  CHECK(f.gate.getState("AirPump"));
  CHECK(!f.gate.stopAirPumpSequence(true));  // debounced
}
