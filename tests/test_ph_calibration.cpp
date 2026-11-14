// 3-point pH calibration (PH_CALIBRATING): drives handlePhCalibrating() through all steps.
// Real code under test: startPhCalibration, handlePhCalibrating, resetCalibrationBatch,
// checkCalibrationStability. Timings: 10 s prompt, 5 batches x 20 readings at 1 s with 30 s
// between batches, stability = last 3 batch means within 0.1 pH, 1 s after each command.
#include <functional>
#include "minitest.h"
#include "fixture.h"

using plantos_controller::ControllerState;

// Calls the handler every 250 ms until the state leaves PH_CALIBRATING or max_ms passes.
// `each` may change the fake sensor before every call.
static void run_calibration(ControllerFixture &f, uint32_t max_ms,
                            const std::function<void(ControllerFixture &)> &each = nullptr) {
  uint32_t start = esphome::millis();
  while (f.ctrl.getCurrentState() == ControllerState::PH_CALIBRATING && esphome::millis() - start < max_ms) {
    if (each) each(f);
    f.ctrl.handlePhCalibrating();
    host::advance_millis(250);
  }
}

static ControllerFixture *start_calibration(ControllerFixture &f) {
  f.set_time(2026, 11, 14, 12, 0);
  f.force_state(ControllerState::IDLE);
  host::set_millis(1000);
  f.hal.has_temperature = true;
  f.hal.ph = 7.0f;
  f.ctrl.startPhCalibration();
  return &f;
}

TEST(ph_calibration, happy_path_mid_low_high_with_exact_timing) {
  ControllerFixture f;
  start_calibration(f);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::PH_CALIBRATING);
  CHECK_EQ(std::string(f.psm.getLastEvent().eventID), std::string("PH_CALIBRATION"));
  run_calibration(f, 3600000);

  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  REQUIRE(f.hal.calib_commands.size() == 3u);
  CHECK_NEAR(f.hal.calib_commands[0].ph, 7.00f, 1e-6);
  CHECK_EQ(f.hal.calib_commands[0].point, 0);
  CHECK_NEAR(f.hal.calib_commands[1].ph, 4.00f, 1e-6);
  CHECK_EQ(f.hal.calib_commands[1].point, 1);
  CHECK_NEAR(f.hal.calib_commands[2].ph, 10.01f, 1e-6);
  CHECK_EQ(f.hal.calib_commands[2].point, 2);
  // Timeline pinned on the code before the table-driven rewrite (2026-11-14): 10 s prompt +
  // 100 readings at 1 s + 0.75 s of tick rounding per point = 110.75 s.
  // SUSPECT: the 30 s wait between batches never happens. After a batch the next reading is
  // taken at once (the reading branch runs before the wait check), so readings_in_batch is
  // 1, not 0, when the wait is tested. Kept as is.
  CHECK_EQ(f.hal.calib_commands[0].at_ms, 110750u);
  CHECK_EQ(f.hal.calib_commands[1].at_ms, 221500u);
  CHECK_EQ(f.hal.calib_commands[2].at_ms, 332250u);
  CHECK_EQ(f.hal.single_readings, 300);       // 3 points x 5 batches x 20
  CHECK_EQ(f.hal.temp_compensations, 3);      // once per point
  CHECK(f.ctrl.calib_step_ == plantos_controller::PlantOSController::CalibrationStep::IDLE);
  CHECK(!f.psm.hasEvent() || std::string(f.psm.getLastEvent().eventID).empty());
  CHECK_EQ(f.ctrl.last_ph_calibration_ts_, f.hal.timestamp);
}

TEST(ph_calibration, unstable_batches_restart_the_point) {
  ControllerFixture f;
  start_calibration(f);
  // Mid point: batch 3 reads 7.3 the first time round -> spread 0.3 -> restart; later stable.
  bool first_round = true;
  run_calibration(f, 3600000, [&](ControllerFixture &g) {
    if (g.ctrl.calib_step_ == plantos_controller::PlantOSController::CalibrationStep::MID_STABILIZING &&
        first_round && g.ctrl.calib_current_batch_ == 3) {
      g.hal.ph = 7.3f;
    } else {
      g.hal.ph = 7.0f;
    }
    if (g.ctrl.calib_current_batch_ == 4) first_round = false;
  });
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  REQUIRE(f.hal.calib_commands.size() == 3u);
  CHECK_EQ(f.hal.calib_commands[0].at_ms, 210250u);  // one extra mid round (pinned on old code)
  CHECK_EQ(f.hal.calib_commands[2].at_ms, 431750u);
  CHECK_EQ(f.hal.single_readings, 400);
}

TEST(ph_calibration, failed_readings_are_retried_without_counting) {
  ControllerFixture f;
  start_calibration(f);
  run_calibration(f, 3600000, [&](ControllerFixture &g) {
    uint32_t t = esphome::millis();
    g.hal.has_ph = !(t >= 20000 && t < 25000) && !(t >= 300000 && t < 302000);  // mid and low gaps
  });
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  REQUIRE(f.hal.calib_commands.size() == 3u);
  CHECK_EQ(f.hal.calib_commands[0].at_ms, 115500u);
  CHECK_EQ(f.hal.calib_commands[1].at_ms, 226250u);
}

TEST(ph_calibration, command_failure_goes_to_error_with_alert) {
  for (int point = 0; point < 3; point++) {
    ControllerFixture f;
    start_calibration(f);
    f.hal.ph_calib_fail_point = point;
    run_calibration(f, 3600000);
    CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::ERROR);
    CHECK_EQ((int)f.hal.calib_commands.size(), point + 1);
    static const char *ALERTS[] = {"CALIBRATION_FAILED_MID", "CALIBRATION_FAILED_LOW", "CALIBRATION_FAILED_HIGH"};
    CHECK(f.ctrl.status_logger_.hasActiveAlerts());
    bool found = false;
    for (auto &a : f.ctrl.status_logger_.getActiveAlerts())
      if (a.type == ALERTS[point]) found = true;
    CHECK(found);
  }
}

TEST(ph_calibration, sensor_lost_mid_run_goes_to_error) {
  ControllerFixture f;
  start_calibration(f);
  run_calibration(f, 100000);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::PH_CALIBRATING);
  f.hal.ph_sensor_ready = false;
  f.ctrl.handlePhCalibrating();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::ERROR);
}

TEST(ph_calibration, refused_without_sensor) {
  ControllerFixture f;
  f.force_state(ControllerState::IDLE);
  f.hal.ph_sensor_ready = false;
  f.ctrl.startPhCalibration();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
}
