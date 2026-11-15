// PSM crash recovery: start an operation (real start*(), so the real event name is logged),
// "reboot" (new objects on the same in-memory NVS) and run INIT once.
// Real code under test: PlantOSController::start*(), handleInit(), the recovery and
// resendRecoveryDrainOff(). Expected behavior: FSMINFO.md "Persistent State Manager (PSM) Events".
#include <functional>
#include <string>
#include <vector>
#include "minitest.h"
#include "fixture.h"

using plantos_controller::ControllerState;
using Ctrl = plantos_controller::PlantOSController;
namespace ev = esphome::persistent_state_manager::psm_event;

namespace {

struct RecordingBackend : alert_service::AlertBackend {
  std::vector<std::string> titles;
  void sendAlert(alert_service::AlertLevel, const std::string &title, const std::string &) override {
    titles.push_back(title);
  }
  bool has(const std::string &t) const {
    for (auto &x : titles)
      if (x == t) return true;
    return false;
  }
};

// The device before the crash: IDLE, start the operation, then power is lost.
void crash_during(const std::function<void(Ctrl &)> &start) {
  ControllerFixture f;
  f.force_state(ControllerState::IDLE);
  start(f.ctrl);
}

// The device after the reboot, with an alert recorder; INIT runs once.
struct Rebooted {
  ControllerFixture f;
  alert_service::AlertService alerts;
  RecordingBackend rec;
  Rebooted() {
    alerts.addBackend(&rec);
    f.ctrl.setAlertService(&alerts);
    host::set_millis(3000);  // INIT_DURATION: recovery runs, INIT leaves
    f.ctrl.handleInit();
  }
  std::string event() const { return f.psm.hasEvent() ? f.psm.getLastEvent().eventID : ""; }
};

struct Case {
  const char *name;
  std::function<void(Ctrl &)> start;
  const char *event;   // what start*() logs
  bool valve_off;      // recovery forces WaterValve OFF
  bool drain_off;      // recovery forces WastewaterPump OFF
  bool cleared;        // recovery clears the PSM event
};

std::vector<Case> cases() {
  return {
      {"fill", [](Ctrl &c) { c.startFillTank(); }, ev::WATER_FILL, true, false, true},
      {"drain", [](Ctrl &c) { c.startEmptyTank(); }, ev::WATER_EMPTY, false, true, true},
      {"feed", [](Ctrl &c) { c.startFeed(); }, ev::FEED_OPERATION, true, false, true},
      {"reservoir_change", [](Ctrl &c) { c.startReservoirChange(); }, ev::RESERVOIR_CHANGE, true, true, true},
      {"ph_correction", [](Ctrl &c) { c.startPhCorrection(); }, ev::PH_CORRECTION, false, false, false},
      {"ph_calibration", [](Ctrl &c) { c.startPhCalibration(); }, ev::PH_CALIBRATION, false, false, false},
      {"feeding", [](Ctrl &c) { c.startFeeding(); }, ev::FEEDING, false, false, false},
      {"mother_mix", [](Ctrl &c) { c.startMotherNutrients(5.0f); }, ev::MOTHER_MIX, false, false, false},
  };
}

}  // namespace

TEST(psm_recovery, start_logs_the_named_event) {
  for (auto &c : cases()) {
    ControllerFixture f;
    f.force_state(ControllerState::IDLE);
    c.start(f.ctrl);
    if (std::string(f.psm.getLastEvent().eventID) != c.event) fprintf(stderr, "    case %s\n", c.name);
    CHECK_EQ(std::string(f.psm.getLastEvent().eventID), std::string(c.event));
  }
}

TEST(psm_recovery, interrupted_operation_after_reboot) {
  for (auto &c : cases()) {
    host::reset();
    crash_during(c.start);
    Rebooted r;
    int valve = r.f.hal.count("valve", "WaterValve", false);
    int drain = r.f.hal.count("pump", "WastewaterPump", false);
    if (valve != (c.valve_off ? 1 : 0) || drain != (c.drain_off ? 1 : 0) || r.event().empty() != c.cleared ||
        !r.rec.has("Operation interrupted"))
      fprintf(stderr, "    case %s: valve %d drain %d event '%s'\n", c.name, valve, drain, r.event().c_str());
    CHECK_EQ(valve, c.valve_off ? 1 : 0);
    CHECK_EQ(drain, c.drain_off ? 1 : 0);
    CHECK_EQ(r.event().empty(), c.cleared);
    if (!c.cleared) CHECK_EQ(r.event(), std::string(c.event));
    CHECK(r.rec.has("Operation interrupted"));
    // No ON command, and no hold state: INIT ends in IDLE
    CHECK_EQ(r.f.hal.count("valve", "WaterValve", true), 0);
    CHECK_EQ(r.f.hal.count("pump", "WastewaterPump", true), 0);
    CHECK_EQ(r.f.ctrl.getCurrentState(), ControllerState::IDLE);
    CHECK_EQ(r.f.ctrl.recovery_drain_off_pending_, c.drain_off);
  }
}

TEST(psm_recovery, hold_states_restore_after_init) {
  for (bool pause : {false, true}) {
    host::reset();
    crash_during([pause](Ctrl &c) {
      if (pause) { c.setToPause(); c.handlePause(); } else { c.setToShutdown(); c.handleShutdown(); }
    });
    Rebooted r;
    CHECK_EQ(r.f.ctrl.getCurrentState(), pause ? ControllerState::PAUSE : ControllerState::SHUTDOWN);
    CHECK(r.rec.has(pause ? "State restored: PAUSE" : "State restored: SHUTDOWN"));
    CHECK_EQ(r.event(), std::string(pause ? ev::STATE_PAUSE : ev::STATE_SHUTDOWN));
    CHECK(!r.rec.has("Operation interrupted"));
    CHECK_EQ(r.f.hal.calls.size(), (size_t)0);
  }
}

TEST(psm_recovery, runs_once_at_the_end_of_init) {
  // The first INIT pass can come long after millis() 0 (component setup); recovery waits
  // for INIT_DURATION and runs exactly once.
  crash_during([](Ctrl &c) { c.startEmptyTank(); });
  ControllerFixture f;
  host::set_millis(1500);
  f.ctrl.handleInit();
  CHECK_EQ(f.hal.count("pump", "WastewaterPump", false), 0);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::INIT);
  host::set_millis(3000);
  f.ctrl.handleInit();
  CHECK_EQ(f.hal.count("pump", "WastewaterPump", false), 1);
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
}

TEST(psm_recovery, clean_boot_does_nothing) {
  Rebooted r;
  CHECK_EQ(r.f.ctrl.getCurrentState(), ControllerState::IDLE);
  CHECK(!r.rec.has("Operation interrupted"));
  CHECK_EQ(r.f.hal.calls.size(), (size_t)0);
}

TEST(psm_recovery, recovery_off_passes_preset_block) {
  // Anzucht has no Water Fill / Drain: the recovery's OFF commands still go out
  crash_during([](Ctrl &c) { c.startReservoirChange(); });
  ControllerFixture f;
  f.gate.setActuatorBlocked("WaterValve", true);
  f.gate.setActuatorBlocked("WastewaterPump", true);
  host::set_millis(3000);
  f.ctrl.handleInit();
  CHECK_EQ(f.hal.count("valve", "WaterValve", false), 1);
  CHECK_EQ(f.hal.count("pump", "WastewaterPump", false), 1);
}

TEST(psm_recovery, drain_off_is_sent_again_once_the_shelly_answers) {
  // At INIT WiFi is not up yet, so the first Shelly OFF usually fails. It is repeated once
  // when the Shelly is reachable.
  crash_during([](Ctrl &c) { c.startEmptyTank(); });
  ControllerFixture f;
  f.hal.shelly_reachable = false;
  host::set_millis(3000);
  f.ctrl.handleInit();
  CHECK_EQ(f.hal.count("pump", "WastewaterPump", false), 1);
  f.ctrl.resendRecoveryDrainOff();
  CHECK_EQ(f.hal.count("pump", "WastewaterPump", false), 1);  // not reachable yet
  f.hal.shelly_reachable = true;
  f.ctrl.resendRecoveryDrainOff();
  CHECK_EQ(f.hal.count("pump", "WastewaterPump", false), 2);
  f.ctrl.resendRecoveryDrainOff();
  CHECK_EQ(f.hal.count("pump", "WastewaterPump", false), 2);  // once only
  CHECK(!f.ctrl.recovery_drain_off_pending_);
}

TEST(psm_recovery, drain_resend_skipped_when_a_new_drain_runs) {
  crash_during([](Ctrl &c) { c.startEmptyTank(); });
  ControllerFixture f;
  f.hal.shelly_reachable = false;
  host::set_millis(3000);
  f.ctrl.handleInit();
  f.force_state(ControllerState::WATER_EMPTYING);
  f.hal.shelly_reachable = true;
  f.ctrl.resendRecoveryDrainOff();
  CHECK_EQ(f.hal.count("pump", "WastewaterPump", false), 1);
  CHECK(!f.ctrl.recovery_drain_off_pending_);
}
