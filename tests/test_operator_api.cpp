// Operator API: the calls behind the web UI entities (former YAML lambdas).
// Real code under test: plantos_controller/operator_api.cpp, ActuatorSafetyGate boot cycling.
#include <cmath>
#include "minitest.h"
#include "fixture.h"
#include "esphome/components/number/number.h"

using plantos_controller::ControllerState;

// ---------------------------------------------------------------------------
// Manual actuator switches
// ---------------------------------------------------------------------------

TEST(operator_api, command_actuator_goes_through_the_gate) {
  ControllerFixture f;
  host::set_millis(1000);
  CHECK(f.ctrl.commandActuator("AcidPump", true));
  CHECK(f.hal.getPumpState("AcidPump"));
  CHECK(f.gate.getState("AcidPump"));
  CHECK(!f.ctrl.commandActuator("AcidPump", true));  // debounced by the gate
  CHECK(f.ctrl.commandActuator("AcidPump", false));
  CHECK(!f.hal.getPumpState("AcidPump"));
  // WaterValve ON is refused by the gate's HIGH interlock
  f.hal.level_high = true;
  CHECK(!f.ctrl.commandActuator("WaterValve", true));
  CHECK(!f.hal.getValveState("WaterValve"));
}

// ---------------------------------------------------------------------------
// Timed tests (were HAL-direct YAML sequences with `delay:`)
// ---------------------------------------------------------------------------

TEST(operator_api, pump_test_runs_10_s_through_the_gate) {
  ControllerFixture f;
  host::set_millis(1000);
  f.ctrl.startPumpTest("NutrientPumpA");
  CHECK(f.hal.getPumpState("NutrientPumpA"));
  CHECK(f.gate.getState("NutrientPumpA"));
  host::advance_millis(9999);
  f.ctrl.updateActuatorTests();
  CHECK(f.hal.getPumpState("NutrientPumpA"));
  host::advance_millis(1);
  f.ctrl.updateActuatorTests();
  CHECK(!f.hal.getPumpState("NutrientPumpA"));
  CHECK(!f.gate.getState("NutrientPumpA"));
  CHECK_EQ(f.hal.count("pump", "NutrientPumpA", true), 1);
  CHECK_EQ(f.hal.count("pump", "NutrientPumpA", false), 1);
}

TEST(operator_api, second_press_while_running_does_not_extend_the_test) {
  ControllerFixture f;
  host::set_millis(1000);
  f.ctrl.startPumpTest("AcidPump");
  host::advance_millis(5000);
  f.ctrl.startPumpTest("AcidPump");
  host::advance_millis(5000);
  f.ctrl.updateActuatorTests();
  CHECK(!f.hal.getPumpState("AcidPump"));  // 10 s after the first press
  CHECK_EQ(f.hal.count("pump", "AcidPump", true), 1);
}

TEST(operator_api, dose_test_duration_comes_from_pumpflow) {
  ControllerFixture f;
  f.hal.setPumpConfig("AcidPump", 2.35f, 1.0f);  // 20 mL / 2.35 mL/s = 8.51 s
  host::set_millis(1000);
  f.ctrl.startPumpDoseTest("AcidPump", 20.0f);
  CHECK(f.hal.getPumpState("AcidPump"));
  host::advance_millis(8509);
  f.ctrl.updateActuatorTests();
  CHECK(f.hal.getPumpState("AcidPump"));
  host::advance_millis(2);
  f.ctrl.updateActuatorTests();
  CHECK(!f.hal.getPumpState("AcidPump"));
}

TEST(operator_api, test_rejected_by_gate_max_duration_does_not_start) {
  ControllerFixture f;
  f.gate.setMaxDuration("AcidPump", 5);  // 10 s request > 5 s max
  host::set_millis(1000);
  f.ctrl.startPumpTest("AcidPump");
  CHECK(!f.hal.getPumpState("AcidPump"));
  host::advance_millis(10000);
  f.ctrl.updateActuatorTests();
  CHECK_EQ(f.hal.count("pump", "AcidPump", false), 0);
}

TEST(operator_api, valve_test_is_3_s) {
  ControllerFixture f;
  f.gate.setMaxDuration("WaterValve", 600);
  host::set_millis(1000);
  f.ctrl.startValveTest();
  CHECK(f.hal.getValveState("WaterValve"));
  host::advance_millis(3000);
  f.ctrl.updateActuatorTests();
  CHECK(!f.hal.getValveState("WaterValve"));
}

TEST(operator_api, emergency_stop_ends_a_running_test) {
  ControllerFixture f;
  host::set_millis(1000);
  f.force_state(ControllerState::IDLE);
  f.ctrl.startPumpTest("NutrientPumpB");
  CHECK(f.hal.getPumpState("NutrientPumpB"));
  f.ctrl.emergencyStop();
  CHECK(!f.hal.getPumpState("NutrientPumpB"));
  // the timer is gone: no second OFF later
  host::advance_millis(20000);
  f.ctrl.updateActuatorTests();
  CHECK_EQ(f.hal.count("pump", "NutrientPumpB", false), 1);
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

TEST(operator_api, air_pump_cycling_switch) {
  ControllerFixture f;
  f.ctrl.setAirPumpCycling(true, 300.0f, 120.0f);
  auto &st = f.gate.actuators_["AirPump"];
  CHECK(st.cyclingEnabled);
  CHECK_EQ(st.cyclingOnPeriod, 300000u);
  CHECK_EQ(st.cyclingOffPeriod, 120000u);
  f.ctrl.setAirPumpCycling(false, 300.0f, 120.0f);
  CHECK(!f.gate.actuators_["AirPump"].cyclingEnabled);
}

TEST(operator_api, gate_boot_cycling_defaults_apply_in_setup_and_start_off) {
  ControllerFixture f;
  f.gate.enableCycling("AirPump", true);  // e.g. restored switch before setup()
  f.gate.setBootCyclingPeriods("AirPump", 120, 60);
  f.gate.setup();
  auto &st = f.gate.actuators_["AirPump"];
  CHECK(!st.cyclingEnabled);
  CHECK_EQ(st.cyclingOnPeriod, 120000u);
  CHECK_EQ(st.cyclingOffPeriod, 60000u);
}

TEST(operator_api, ph_temperature_compensation) {
  ControllerFixture f;
  f.hal.has_temperature = false;
  f.ctrl.sendPhTempCompensationFromSensor();
  CHECK_EQ(f.hal.temp_compensations, 0);
  f.hal.has_temperature = true;
  f.ctrl.sendPhTempCompensationFromSensor();
  CHECK_EQ(f.hal.temp_compensations, 1);
  f.ctrl.sendPhTempCompensation(19.5f);
  CHECK_EQ(f.hal.temp_compensations, 2);
}

TEST(operator_api, k_factor_updates_reach_the_number) {
  ControllerFixture f;
  esphome::number::Number slider;
  f.ctrl.set_ph_k_number(&slider);
  f.ctrl.ph_k_update_callback_(0.3125f);
  CHECK_NEAR(slider.state, 0.3125f, 1e-6);
}

TEST(operator_api, ec_calibration_with_target) {
  ControllerFixture f;
  f.force_state(ControllerState::IDLE);
  f.ctrl.startEcCalibration(2000.0f);
  CHECK_NEAR(f.ctrl.ec_calib_target_, 2000.0f, 1e-3);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::EC_CALIBRATING);
}

TEST(operator_api, grow_day_plus_minus) {
  ControllerFixture f;
  uint8_t d = f.cal.get_current_day();
  CHECK(f.ctrl.changeGrowDay(+1));
  CHECK_EQ((int)f.cal.get_current_day(), d + 1);
  CHECK(f.ctrl.changeGrowDay(-1));
  CHECK_EQ((int)f.cal.get_current_day(), (int)d);
  CHECK(!f.ctrl.changeGrowDay(0));
}

// ---------------------------------------------------------------------------
// Text sensors: same text as the former YAML lambdas
// ---------------------------------------------------------------------------

TEST(operator_api, uptime_text) {
  ControllerFixture f;
  host::set_millis(3661999);  // 1 h 1 min 1.999 s
  CHECK_EQ(f.ctrl.uptimeText(), std::string("01:01:01"));
  host::set_millis(0);
  CHECK_EQ(f.ctrl.uptimeText(), std::string("00:00:00"));
  host::set_millis(100u * 3600u * 1000u);  // 100 h: the field just grows
  CHECK_EQ(f.ctrl.uptimeText(), std::string("100:00:00"));
}

TEST(operator_api, tds_text) {
  ControllerFixture f;
  f.hal.ec = 1001.0f;  // 500.5 ppm -> "%.0f" rounds half to even
  CHECK_EQ(f.ctrl.tdsText(), std::string("500 ppm"));
  f.hal.ec = 1234.0f;
  CHECK_EQ(f.ctrl.tdsText(), std::string("617 ppm"));
  f.hal.has_ec = false;
  CHECK_EQ(f.ctrl.tdsText(), std::string("N/A"));
}

TEST(operator_api, tank_volume_text) {
  ControllerFixture f;
  CHECK_EQ(f.ctrl.tankVolumeText(), std::string("Delta: 27.4L (LOW→HIGH) | Total: 47.5L (EMPTY→HIGH)"));
}

TEST(operator_api, current_time_text) {
  ControllerFixture f;
  f.clock.set_invalid();
  CHECK_EQ(f.ctrl.currentTimeText(), std::string("Not initialized"));
  f.set_time(2026, 3, 7, 9, 5, 4, 3600);
  CHECK_EQ(f.ctrl.currentTimeText(), std::string("2026-03-07 09:05:04"));
}

TEST(operator_api, grow_day_text) {
  ControllerFixture f;
  char expected[32];
  snprintf(expected, sizeof(expected), "Day %d / 120", f.cal.get_current_day());
  CHECK_EQ(f.ctrl.growDayText(), std::string(expected));
}
