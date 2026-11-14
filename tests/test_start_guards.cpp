// Start guards of the public API (start*()): which state and hardware each start needs,
// and the state it enters. Real code under test: PlantOSController::start*() / canStart().
#include <functional>
#include "minitest.h"
#include "fixture.h"

using plantos_controller::ControllerState;
using Ctrl = plantos_controller::PlantOSController;

struct StartCase {
  const char *name;
  std::function<void(Ctrl &)> start;
  ControllerState target;
};

static std::vector<StartCase> start_cases() {
  return {
      {"ph_correction", [](Ctrl &c) { c.startPhCorrection(); }, ControllerState::PH_MEASURING},
      {"ph_calibration", [](Ctrl &c) { c.startPhCalibration(); }, ControllerState::PH_CALIBRATING},
      {"feeding", [](Ctrl &c) { c.startFeeding(); }, ControllerState::FEEDING},
      {"mother_mix", [](Ctrl &c) { c.startMotherNutrients(5.0f); }, ControllerState::EC_FEEDING},
      {"fill", [](Ctrl &c) { c.startFillTank(); }, ControllerState::WATER_FILLING},
      {"drain", [](Ctrl &c) { c.startEmptyTank(); }, ControllerState::WATER_EMPTYING},
      {"feed", [](Ctrl &c) { c.startFeed(); }, ControllerState::FEED_FILLING},
      {"ec_check", [](Ctrl &c) { c.startEcCheck(); }, ControllerState::EC_PROCESSING},
      {"ec_calibration", [](Ctrl &c) { c.startEcCalibration(); }, ControllerState::EC_CALIBRATING},
      {"reservoir_change", [](Ctrl &c) { c.startReservoirChange(); }, ControllerState::WATER_EMPTYING},
  };
}

TEST(start_guards, idle_starts_every_operation) {
  for (auto &c : start_cases()) {
    ControllerFixture f;
    f.force_state(ControllerState::IDLE);
    c.start(f.ctrl);
    if (f.ctrl.getCurrentState() != c.target) fprintf(stderr, "    case %s\n", c.name);
    CHECK_EQ(f.ctrl.getCurrentState(), c.target);
  }
}

TEST(start_guards, night_and_busy_states_refuse_every_operation) {
  for (ControllerState s : {ControllerState::NIGHT, ControllerState::PH_MIXING, ControllerState::SHUTDOWN,
                            ControllerState::PAUSE, ControllerState::ERROR, ControllerState::WATER_FILLING}) {
    for (auto &c : start_cases()) {
      ControllerFixture f;
      f.force_state(s);
      c.start(f.ctrl);
      CHECK_EQ(f.ctrl.getCurrentState(), s);
      CHECK(!f.ctrl.auto_ph_correction_pending_);
    }
  }
}

TEST(start_guards, hardware_requirements) {
  {  // fill needs level sensors
    ControllerFixture f;
    f.force_state(ControllerState::IDLE);
    f.hal.has_level_sensors = false;
    f.ctrl.startFillTank();
    CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
    CHECK(!f.ctrl.auto_ph_correction_pending_);
  }
  {  // drain and reservoir change need the Shelly
    ControllerFixture f;
    f.force_state(ControllerState::IDLE);
    f.hal.shelly_enabled = false;
    f.ctrl.startEmptyTank();
    CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
    f.ctrl.startReservoirChange();
    CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
    CHECK(!f.ctrl.auto_ph_correction_pending_);
  }
  {  // EC calibration needs an EC reading and raises an alert without one
    ControllerFixture f;
    f.force_state(ControllerState::IDLE);
    f.hal.has_ec = false;
    f.ctrl.startEcCalibration();
    CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
    CHECK(f.ctrl.status_logger_.hasActiveAlerts());
  }
  {  // feed refuses a tank that is not empty, but runs without level sensors
    ControllerFixture f;
    f.force_state(ControllerState::IDLE);
    f.hal.level_low = true;
    f.ctrl.startFeed();
    CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
    CHECK(!f.ctrl.auto_ph_correction_pending_);
    f.hal.has_level_sensors = false;
    f.ctrl.startFeed();
    CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::FEED_FILLING);
    CHECK(f.ctrl.auto_ph_correction_pending_);
    CHECK(!f.ctrl.is_reservoir_change_);
  }
  {  // mother mix volume range
    ControllerFixture f;
    f.force_state(ControllerState::IDLE);
    f.ctrl.startMotherNutrients(0.0f);
    f.ctrl.startMotherNutrients(15.5f);
    CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
    f.ctrl.startMotherNutrients(15.0f);
    CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::EC_FEEDING);
    CHECK_NEAR(f.ctrl.ec_dose_A_ml_, 21.6f, 1e-4);  // 1.44 mL/L * 15 L
  }
  {  // reservoir change sets its context and the PSM event
    ControllerFixture f;
    f.force_state(ControllerState::IDLE);
    f.ctrl.startReservoirChange();
    CHECK(f.ctrl.is_reservoir_change_);
    CHECK(f.ctrl.auto_ph_correction_pending_);
    CHECK_EQ(std::string(f.psm.getLastEvent().eventID), std::string("RESERVOIR_CHANGE"));
  }
}
