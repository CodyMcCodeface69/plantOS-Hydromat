// Runtime presets, part 2: the controller side (presets.cpp + the gates in controller.cpp,
// scheduling.cpp, ph_control.cpp, ec_control.cpp, operator_api.cpp) and the safety gate's
// preset block. With the default preset (Haupt, all functions) every other test file is the
// "Haupt equals today" regression.
#include <functional>
#include <string>
#include "minitest.h"
#include "fixture.h"

using plantos_controller::ControllerState;
using Ctrl = plantos_controller::PlantOSController;
using namespace plantos_controller::preset_model;

namespace {

struct OpCase {
  const char *name;
  std::function<void(Ctrl &)> start;
  ControllerState target;
  uint8_t needs;  // the capability table, written out by hand
};

std::vector<OpCase> op_cases() {
  return {
      {"ph_correction", [](Ctrl &c) { c.startPhCorrection(); }, ControllerState::PH_MEASURING, 0},
      {"ph_calibration", [](Ctrl &c) { c.startPhCalibration(); }, ControllerState::PH_CALIBRATING, 0},
      {"feeding", [](Ctrl &c) { c.startFeeding(); }, ControllerState::FEEDING, FN_NUTRIENTS},
      {"mother_mix", [](Ctrl &c) { c.startMotherNutrients(5.0f); }, ControllerState::EC_FEEDING, FN_NUTRIENTS},
      {"fill", [](Ctrl &c) { c.startFillTank(); }, ControllerState::WATER_FILLING, FN_WATER_FILL},
      {"drain", [](Ctrl &c) { c.startEmptyTank(); }, ControllerState::WATER_EMPTYING, FN_DRAIN},
      {"feed", [](Ctrl &c) { c.startFeed(); }, ControllerState::FEED_FILLING, FN_WATER_FILL | FN_NUTRIENTS},
      {"ec_check", [](Ctrl &c) { c.startEcCheck(); }, ControllerState::EC_PROCESSING, FN_NUTRIENTS},
      {"ec_calibration", [](Ctrl &c) { c.startEcCalibration(); }, ControllerState::EC_CALIBRATING, 0},
      {"reservoir_change", [](Ctrl &c) { c.startReservoirChange(); }, ControllerState::WATER_EMPTYING,
       FN_DRAIN | FN_WATER_FILL | FN_NUTRIENTS},
  };
}

// Set the active preset's function mask directly (no state checks, no actuator side effects)
void set_mask(ControllerFixture &f, uint8_t mask) {
  auto v = f.ctrl.profile_.current();
  v.mask = mask;
  f.ctrl.profile_.set_current(v);
  f.ctrl.applyPresetToHardware();
}

bool contains(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }

const ControllerState IDLE_TYPE[] = {ControllerState::IDLE, ControllerState::NIGHT, ControllerState::SHUTDOWN,
                                     ControllerState::PAUSE, ControllerState::ERROR};
const ControllerState BUSY[] = {ControllerState::INIT,          ControllerState::PH_PROCESSING,
                                ControllerState::PH_MEASURING,  ControllerState::PH_CALCULATING,
                                ControllerState::PH_INJECTING,  ControllerState::PH_MIXING,
                                ControllerState::PH_CALIBRATING, ControllerState::FEEDING,
                                ControllerState::WATER_FILLING, ControllerState::WATER_EMPTYING,
                                ControllerState::FEED_FILLING,  ControllerState::EC_PROCESSING,
                                ControllerState::EC_CALCULATING, ControllerState::EC_FEEDING,
                                ControllerState::EC_MIXING,     ControllerState::EC_MEASURING,
                                ControllerState::EC_CALIBRATING};

}  // namespace

// ---------------------------------------------------------------------------
// Capability gate
// ---------------------------------------------------------------------------

TEST(preset_gate, operation_table) {
  using Op = Ctrl::Operation;
  CHECK_EQ(Ctrl::operationFunctions(Op::PH_CORRECTION), 0);
  CHECK_EQ(Ctrl::operationFunctions(Op::PH_CALIBRATION), 0);
  CHECK_EQ(Ctrl::operationFunctions(Op::EC_CALIBRATION), 0);
  CHECK_EQ(Ctrl::operationFunctions(Op::FEEDING), FN_NUTRIENTS);
  CHECK_EQ(Ctrl::operationFunctions(Op::MOTHER_MIX), FN_NUTRIENTS);
  CHECK_EQ(Ctrl::operationFunctions(Op::EC_CHECK), FN_NUTRIENTS);
  CHECK_EQ(Ctrl::operationFunctions(Op::FILL), FN_WATER_FILL);
  CHECK_EQ(Ctrl::operationFunctions(Op::DRAIN), FN_DRAIN);
  CHECK_EQ(Ctrl::operationFunctions(Op::FEED), FN_WATER_FILL | FN_NUTRIENTS);
  CHECK_EQ(Ctrl::operationFunctions(Op::RESERVOIR_CHANGE), FN_DRAIN | FN_WATER_FILL | FN_NUTRIENTS);
}

TEST(preset_gate, every_operation_with_each_function_missing) {
  for (uint8_t i = 0; i < FN_COUNT; i++) {
    uint8_t missing = 1u << i;
    for (auto &c : op_cases()) {
      ControllerFixture f;
      f.force_state(ControllerState::IDLE);
      set_mask(f, FN_ALL & ~missing);
      c.start(f.ctrl);
      bool should_refuse = (c.needs & missing) != 0;
      if ((f.ctrl.getCurrentState() == c.target) == should_refuse) {
        fprintf(stderr, "    case %s without %s\n", c.name, functionName(missing));
      }
      CHECK_EQ(f.ctrl.getCurrentState(), should_refuse ? ControllerState::IDLE : c.target);
      if (should_refuse) {
        CHECK(!f.ctrl.auto_ph_correction_pending_);
        CHECK(contains(f.ctrl.lastRefusalText(), "not available in preset Haupt (needs "));
        CHECK(contains(f.ctrl.lastRefusalText(), functionName(missing)));
      }
    }
  }
}

TEST(preset_gate, refusal_reason_names_all_missing_functions) {
  ControllerFixture f;
  f.force_state(ControllerState::IDLE);
  set_mask(f, FN_NUTRIENTS);
  f.ctrl.startReservoirChange();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  CHECK_EQ(f.ctrl.lastRefusalText(),
           std::string("reservoir change: not available in preset Haupt (needs Water Fill + Drain)"));
}

TEST(preset_gate, last_refusal_starts_as_none_and_carries_the_clock) {
  ControllerFixture f;
  CHECK_EQ(f.ctrl.lastRefusalText(), std::string("None"));
  f.set_time(2026, 11, 14, 22, 30);
  f.force_state(ControllerState::NIGHT);
  f.ctrl.startFeed();
  CHECK_EQ(f.ctrl.lastRefusalText(), std::string("22:30 feed: night mode"));
}

TEST(preset_gate, pure_ph_preset_still_corrects_ph) {
  ControllerFixture f;
  f.force_state(ControllerState::IDLE);
  set_mask(f, 0);  // Anzucht default
  f.ctrl.startPhCorrection();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::PH_MEASURING);
}

// ---------------------------------------------------------------------------
// Automatic triggers
// ---------------------------------------------------------------------------

static void arm_low_level(ControllerFixture &f) {
  f.force_state(ControllerState::IDLE);
  f.set_time(2026, 11, 14, 18, 0);  // light on (not night) in every calendar
  f.hal.level_high = false;
  f.hal.level_low = false;
  f.hal.level_empty = true;
  f.ctrl.auto_feeding_enabled_ = true;
  f.ctrl.auto_ph_correction_enabled_ = false;
}

TEST(preset_auto, feed_needs_fill_and_nutrients_else_auto_fill_tops_up) {
  ControllerFixture f;
  arm_low_level(f);
  f.ctrl.auto_fill_enabled_ = true;
  set_mask(f, FN_WATER_FILL);  // water but no nutrient pumps
  CHECK(!f.ctrl.shouldTriggerAutoFeeding());
  f.ctrl.handleIdle();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::WATER_FILLING);  // auto-fill, not a half feed
  CHECK_EQ(f.ctrl.lastAutoFeedDay(), 0);                                // no feed day marked
  CHECK_EQ(f.ctrl.lastRefusalText(), std::string("None"));               // silent, no refusal spam
}

TEST(preset_auto, nothing_without_water_fill) {
  ControllerFixture f;
  arm_low_level(f);
  f.ctrl.auto_fill_enabled_ = true;
  set_mask(f, FN_NUTRIENTS | FN_DRAIN);
  f.ctrl.handleIdle();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::IDLE);
  CHECK_EQ(f.ctrl.lastRefusalText(), std::string("None"));
}

TEST(preset_auto, reservoir_change_needs_drain_fill_and_nutrients) {
  for (uint8_t missing : {FN_DRAIN, FN_WATER_FILL, FN_NUTRIENTS}) {
    ControllerFixture f;
    f.force_state(ControllerState::IDLE);
    f.set_time(2026, 11, 14, 18, 0);  // Saturday
    f.ctrl.auto_reservoir_change_enabled_ = true;
    f.ctrl.auto_reservoir_change_day_ = 6;
    CHECK(f.ctrl.shouldTriggerAutoReservoirChange());
    set_mask(f, FN_ALL & ~missing);
    CHECK(!f.ctrl.shouldTriggerAutoReservoirChange());
  }
}

TEST(preset_auto, post_fill_ec_check_without_nutrients_goes_on_to_ph) {
  ControllerFixture f;
  set_mask(f, FN_WATER_FILL);
  f.ctrl.auto_ec_check_pending_ = true;
  f.ctrl.auto_ph_correction_pending_ = true;
  f.force_state(ControllerState::EC_PROCESSING);
  f.ctrl.handleEcProcessing();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::PH_PROCESSING);
  CHECK(!f.ctrl.auto_ec_check_pending_);
  CHECK_EQ(f.hal.count("pump", "NutrientPumpA", true), 0);
}

TEST(preset_auto, feed_and_reservoir_markers_are_per_preset) {
  ControllerFixture f;
  arm_low_level(f);
  f.ctrl.setupPresets();
  f.ctrl.markAutoFeedDay(f.ctrl.getLocalDateOrdinal());  // Haupt fed today
  f.ctrl.markAutoResWeek(f.ctrl.getCurrentWeekNumber());
  CHECK(!f.ctrl.shouldTriggerAutoFeeding());
  CHECK(f.ctrl.requestPreset(MUTTER));
  CHECK(f.ctrl.shouldTriggerAutoFeeding());  // Mutter has not been fed today
  CHECK_EQ(f.ctrl.lastAutoResWeek(), 0);
  CHECK_EQ(f.psm.loadInt32("AutoFeedDay", 0), 20771);  // Haupt keeps the old key
  CHECK_EQ(f.psm.loadInt32("AutoFeedDay1", 0), 0);
  f.ctrl.markAutoFeedDay(20771);
  CHECK_EQ(f.psm.loadInt32("AutoFeedDay1", 0), 20771);
  CHECK(f.ctrl.requestPreset(HAUPT));
  CHECK(!f.ctrl.shouldTriggerAutoFeeding());
  CHECK_EQ(f.ctrl.lastAutoResWeek(), f.ctrl.getCurrentWeekNumber());
}

// ---------------------------------------------------------------------------
// Air pump and grow light
// ---------------------------------------------------------------------------

TEST(preset_air, mixing_settles_longer_without_air_pump) {
  ControllerFixture f;
  f.hal.tank_volume_delta = 27.4f;
  CHECK_NEAR(f.ctrl.calculatePhMixingDuration(), 244442, 2);  // Haupt: as today
  set_mask(f, FN_ALL & ~FN_AIR_PUMP);
  f.hal.tank_volume_delta = 27.4f;  // set_mask pushed the preset volume (also 27.4)
  CHECK_NEAR(f.ctrl.calculatePhMixingDuration(), 488884, 4);  // x2
  f.hal.tank_volume_delta = 70.0f;
  CHECK_NEAR(f.ctrl.calculatePhMixingDuration(), 1199798, 4);  // 599899 x2, just below the cap
  f.hal.tank_volume_delta = 500.0f;
  CHECK_EQ(f.ctrl.calculatePhMixingDuration(), 1200000u);  // 600 s x2 = 20 min cap
  f.ctrl.setNoAirPumpSettleFactor(3.0f);
  CHECK_EQ(f.ctrl.calculatePhMixingDuration(), 1200000u);  // capped
  f.hal.tank_volume_delta = 1.0f;
  CHECK_EQ(f.ctrl.calculatePhMixingDuration(), 90000u);  // 30 s minimum x3
}

TEST(preset_air, ec_mixing_waits_factor_times_longer) {
  for (bool air : {true, false}) {
    ControllerFixture f;
    if (!air) set_mask(f, FN_ALL & ~FN_AIR_PUMP);
    f.force_state(ControllerState::EC_MIXING);
    f.ctrl.handleEcMixing();
    host::advance_millis(300000);
    f.ctrl.handleEcMixing();
    CHECK_EQ(f.ctrl.getCurrentState(), air ? ControllerState::EC_MEASURING : ControllerState::EC_MIXING);
    host::advance_millis(300000);
    f.ctrl.handleEcMixing();
    CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::EC_MEASURING);
  }
}

TEST(preset_air, ph_injection_and_mixing_never_command_the_air_pump) {
  ControllerFixture f;
  set_mask(f, FN_ALL & ~FN_AIR_PUMP);
  f.ctrl.ph_dose_duration_ms_ = 1000;
  f.ctrl.ph_dose_ml_ = 2.35f;
  f.force_state(ControllerState::PH_INJECTING);
  for (int i = 0; i < 4; i++) {
    f.ctrl.handlePhInjecting();
    host::advance_millis(600);
  }
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::PH_MIXING);
  f.ctrl.handlePhMixing();
  host::advance_millis(f.ctrl.ph_mixing_duration_ms_);
  f.ctrl.handlePhMixing();
  CHECK_EQ(f.ctrl.getCurrentState(), ControllerState::PH_MEASURING);
  for (auto &c : f.hal.calls) CHECK(c.id != "AirPump");
}

TEST(preset_air, wifi_activation_and_cycling_switch_send_nothing) {
  ControllerFixture f;
  set_mask(f, FN_NUTRIENTS);
  f.ctrl.activateAirPump();
  f.ctrl.setAirPumpCycling(true, 120, 60);
  CHECK(f.hal.calls.empty());
  CHECK(f.gate.isCyclingEnabled("AirPump"));  // the mode is stored for later
}

TEST(preset_light, grow_light_never_commanded_without_the_function) {
  ControllerFixture f;
  set_mask(f, FN_ALL & ~FN_GROW_LIGHT);
  f.force_state(ControllerState::IDLE);
  f.ctrl.setGrowLightScheduleEnabled(true);
  f.set_time(2026, 11, 14, 18, 0);  // light window: on
  f.ctrl.controlGrowLight();
  f.set_time(2026, 11, 14, 9, 0);  // off
  f.ctrl.controlGrowLight();
  CHECK_EQ(f.hal.count("pump", "GrowLight", true), 0);
  CHECK_EQ(f.hal.count("pump", "GrowLight", false), 0);
  // The calendar's light window still defines night for night mode
  f.set_time(2026, 11, 14, 12, 0);
  CHECK(f.ctrl.isNightModeHours());
}

TEST(preset_light, manual_switch_and_test_refused_with_reason) {
  ControllerFixture f;
  set_mask(f, FN_ALL & ~FN_GROW_LIGHT);
  CHECK(!f.ctrl.commandActuator("GrowLight", true));
  CHECK(contains(f.ctrl.lastRefusalText(), "GrowLight: not available in preset Haupt (needs Grow Light)"));
  CHECK(f.ctrl.commandActuator("AcidPump", true));  // pH pump always
  f.ctrl.startPumpTest("NutrientPumpA");
  CHECK_EQ(f.hal.count("pump", "NutrientPumpA", true), 1);
  set_mask(f, 0);
  f.ctrl.startValveTest();
  CHECK_EQ(f.hal.count("valve", "WaterValve", true), 0);
  CHECK(contains(f.ctrl.lastRefusalText(), "Actuator test: not available"));
}

// ---------------------------------------------------------------------------
// Safety gate: second line of defense
// ---------------------------------------------------------------------------

TEST(preset_gate_asg, blocked_actuator_rejects_on_but_passes_off) {
  ControllerFixture f;
  f.gate.setActuatorBlocked("WaterValve", true);
  CHECK(!f.gate.isActuatorAllowed("WaterValve"));
  CHECK(!f.gate.executeCommand("WaterValve", true, 10));
  CHECK(!f.gate.executeCommand("WaterValve", true, 10, true));  // forced too
  CHECK_EQ(f.hal.count("valve", "WaterValve", true), 0);
  f.gate.setActuatorBlocked("WaterValve", false);
  CHECK(f.gate.executeCommand("WaterValve", true, 10));
  f.gate.setActuatorBlocked("WaterValve", true);
  CHECK(f.gate.executeCommand("WaterValve", false));  // OFF always passes
  CHECK_EQ(f.hal.count("valve", "WaterValve", false), 1);
}

TEST(preset_gate_asg, air_pump_pattern_and_on_are_rejected) {
  ControllerFixture f;
  f.gate.executeCommand("AirPump", false, 0, true);
  f.gate.updateActualState("AirPump", false);  // as after the first Shelly poll
  f.gate.setActuatorBlocked("AirPump", true);
  CHECK(!f.gate.setAirPumpPattern({60, 30}, true));
  CHECK(!f.gate.stopAirPumpSequence(true));
  f.gate.enableCycling("AirPump", false);  // "normal mode" would turn it on
  f.gate.setCyclingPeriods("AirPump", 120, 60);
  f.gate.enableCycling("AirPump", true);
  CHECK(f.gate.isCyclingEnabled("AirPump"));
  CHECK(!f.gate.shelly_cycling_active_);
  for (auto &c : f.hal.calls) CHECK(!(c.id == "AirPump" && c.state));
}

TEST(preset_gate_asg, controller_pushes_the_mask) {
  ControllerFixture f;
  f.ctrl.setupPresets();  // Haupt: nothing blocked
  for (const char *id : {"NutrientPumpA", "WaterValve", "WastewaterPump", "AirPump", "GrowLight", "AcidPump"})
    CHECK(f.gate.isActuatorAllowed(id));
  f.force_state(ControllerState::IDLE);
  f.ctrl.requestPreset(ANZUCHT);
  for (const char *id : {"NutrientPumpA", "NutrientPumpB", "NutrientPumpC", "WaterValve", "WastewaterPump",
                         "AirPump", "GrowLight"})
    CHECK(!f.gate.isActuatorAllowed(id));
  CHECK(f.gate.isActuatorAllowed("AcidPump"));
}

// ---------------------------------------------------------------------------
// Switching
// ---------------------------------------------------------------------------

TEST(preset_switch, allowed_in_idle_type_states_refused_in_busy_states) {
  for (ControllerState s : IDLE_TYPE) {
    host::reset();  // fresh NVS: each case starts at first boot
    ControllerFixture f;
    f.ctrl.setupPresets();
    f.force_state(s);
    CHECK(f.ctrl.requestPreset(MUTTER));
    CHECK_EQ(f.ctrl.activePreset(), MUTTER);
    CHECK_EQ(f.ctrl.getCurrentState(), s);
  }
  for (ControllerState s : BUSY) {
    host::reset();
    ControllerFixture f;
    f.ctrl.setupPresets();
    f.force_state(s);
    CHECK(!f.ctrl.requestPreset(MUTTER));
    CHECK_EQ(f.ctrl.activePreset(), HAUPT);
    CHECK(contains(f.ctrl.lastRefusalText(), "Preset Mutter: operation running ("));
    CHECK(!f.ctrl.requestPresetByName("Anzucht"));
  }
}

TEST(preset_switch, same_preset_and_unknown_name) {
  ControllerFixture f;
  f.ctrl.setupPresets();
  f.force_state(ControllerState::PH_MIXING);
  CHECK(f.ctrl.requestPreset(HAUPT));  // no-op, even while busy
  f.force_state(ControllerState::IDLE);
  CHECK(!f.ctrl.requestPresetByName("Bloom"));
  CHECK_EQ(f.ctrl.activePreset(), HAUPT);
  CHECK(f.ctrl.requestPresetByName("Mutter"));
  CHECK_EQ(f.ctrl.activePresetName(), std::string("Mutter"));
}

TEST(preset_switch, swaps_volumes_calendar_and_day_and_turns_actuators_off) {
  host::set_millis(1000);  // the gate treats a Shelly sync at millis 0 as "never polled"
  ControllerFixture f;
  f.set_time(2026, 11, 14, 18, 0);
  f.ctrl.setupPresets();
  f.force_state(ControllerState::IDLE);
  f.cal.sync_to_date(f.ctrl.getLocalDateOrdinal());
  f.cal.set_current_day(69);  // Haupt day 69
  CHECK(f.ctrl.commandActuator("NutrientPumpA", true));
  CHECK(f.ctrl.commandActuator("WastewaterPump", true));
  f.gate.executeCommand("AirPump", true, 0, true);
  f.gate.updateActualState("AirPump", true);
  f.ctrl.auto_ph_correction_pending_ = true;
  f.ctrl.auto_ec_check_pending_ = true;
  f.ctrl.last_ec_feeding_timestamp_ = 12345;
  f.hal.calls.clear();

  CHECK(f.ctrl.requestPreset(MUTTER));
  CHECK_NEAR(f.hal.tank_volume_delta, 10.0f, 1e-6);
  CHECK_NEAR(f.hal.total_tank_volume, 15.0f, 1e-6);
  CHECK_EQ(f.cal.get_active_calendar(), MUTTER);
  CHECK_EQ(f.cal.get_current_day(), 1);  // Mutter's own day (seeded today)
  CHECK_EQ(f.hal.count("pump", "NutrientPumpA", false), 1);
  CHECK_EQ(f.hal.count("pump", "WastewaterPump", false), 1);
  CHECK_EQ(f.hal.count("stop_sequence", "AirPump", false), 1);  // Mutter has no air pump
  CHECK_EQ(f.hal.count("pump", "GrowLight", false), 0);          // was not on
  CHECK(!f.ctrl.auto_ph_correction_pending_);
  CHECK(!f.ctrl.auto_ec_check_pending_);
  CHECK_EQ(f.ctrl.last_ec_feeding_timestamp_, 0);
  CHECK(f.ctrl.grow_light_first_run_);

  // back to Haupt: its day is still 69, the air pump mode is restored (normal mode = ON)
  f.hal.calls.clear();
  CHECK(f.ctrl.requestPreset(HAUPT));
  CHECK_EQ(f.cal.get_current_day(), 69);
  CHECK_NEAR(f.hal.tank_volume_delta, 27.4f, 1e-6);
  CHECK_EQ(f.hal.count("stop_sequence", "AirPump", true), 1);
}

TEST(preset_switch, keeps_air_pump_and_light_when_the_new_preset_has_them) {
  host::set_millis(1000);
  ControllerFixture f;
  f.ctrl.setupPresets();
  f.force_state(ControllerState::IDLE);
  set_mask(f, FN_ALL);  // stays Haupt; give Mutter the same Shelly functions via its record
  f.ctrl.profile_.set_defaults(MUTTER, 10.0f, 15.0f, FN_ALL);
  f.gate.executeCommand("AirPump", true, 0, true);
  f.gate.updateActualState("AirPump", true);
  f.gate.executeCommand("GrowLight", true, 0);
  f.hal.calls.clear();
  CHECK(f.ctrl.requestPreset(MUTTER));
  CHECK_EQ(f.hal.count("stop_sequence", "AirPump", false), 0);
  CHECK_EQ(f.hal.count("pump", "GrowLight", false), 0);
}

TEST(preset_switch, light_off_when_the_new_preset_lacks_it) {
  ControllerFixture f;
  f.ctrl.setupPresets();
  f.force_state(ControllerState::IDLE);
  f.gate.executeCommand("GrowLight", true, 0);
  CHECK(f.ctrl.requestPreset(MUTTER));
  CHECK_EQ(f.hal.count("pump", "GrowLight", false), 1);
}

TEST(preset_switch, everything_is_back_after_a_reboot) {
  {
    ControllerFixture f;
    f.set_time(2026, 11, 14, 18, 0);
    f.ctrl.setupPresets();
    f.force_state(ControllerState::IDLE);
    CHECK(f.ctrl.requestPreset(MUTTER));
    CHECK(f.ctrl.setPresetVolumeTotal(20.0f));
    CHECK(f.ctrl.setPresetVolumeDelta(12.5f));
    CHECK(f.ctrl.setPresetFunction(FN_AIR_PUMP, true));
    f.cal.set_current_day(400);
  }
  ControllerFixture g;  // reboot: same NVS
  g.set_time(2026, 11, 14, 18, 30);
  g.ctrl.setupPresets();
  CHECK_EQ(g.ctrl.activePreset(), MUTTER);
  CHECK_EQ(g.cal.get_active_calendar(), MUTTER);
  CHECK_EQ(g.cal.get_current_day(), 400);
  CHECK_NEAR(g.hal.tank_volume_delta, 12.5f, 1e-6);
  CHECK_NEAR(g.hal.total_tank_volume, 20.0f, 1e-6);
  CHECK(g.ctrl.hasFunction(FN_AIR_PUMP));
  CHECK(g.ctrl.hasFunction(FN_NUTRIENTS | FN_WATER_FILL | FN_DRAIN));
  CHECK(!g.ctrl.hasFunction(FN_GROW_LIGHT));
  CHECK(!g.gate.isActuatorAllowed("GrowLight"));
  // Haupt untouched
  CHECK_NEAR(g.ctrl.profile_.values(HAUPT).delta_l, 27.4f, 1e-6);
  CHECK_EQ(g.ctrl.profile_.values(HAUPT).mask, FN_ALL);
}

TEST(preset_switch, first_boot_is_haupt_as_today) {
  ControllerFixture f;
  f.hal.tank_volume_delta = 1.0f;  // whatever the HAL had: the preset sets it
  int saves_before = host::nvs_save_count;
  f.ctrl.setupPresets();
  CHECK_EQ(f.ctrl.activePreset(), HAUPT);
  CHECK_EQ(f.cal.get_active_calendar(), HAUPT);
  CHECK_NEAR(f.hal.tank_volume_delta, 27.4f, 1e-6);
  CHECK_NEAR(f.hal.total_tank_volume, 47.5f, 1e-6);
  CHECK(f.ctrl.hasFunction(FN_ALL));
  CHECK_EQ(host::nvs_save_count, saves_before);  // nothing written by the preset store
}

// ---------------------------------------------------------------------------
// Editing the active preset
// ---------------------------------------------------------------------------

TEST(preset_edit, volumes_persist_apply_and_leave_others_alone) {
  ControllerFixture f;
  f.ctrl.setupPresets();
  f.force_state(ControllerState::NIGHT);
  CHECK(f.ctrl.setPresetVolumeDelta(25.0f));
  CHECK_NEAR(f.hal.tank_volume_delta, 25.0f, 1e-6);
  CHECK_NEAR(f.ctrl.presetVolumeDelta(), 25.0f, 1e-6);
  CHECK(f.ctrl.profile_.is_stored(HAUPT));
  CHECK(!f.ctrl.profile_.is_stored(MUTTER));
  CHECK_NEAR(f.ctrl.profile_.values(MUTTER).delta_l, 10.0f, 1e-6);
  // invalid: larger than EMPTY->HIGH, or out of range
  CHECK(!f.ctrl.setPresetVolumeDelta(60.0f));
  CHECK(contains(f.ctrl.lastRefusalText(), "Tank Volume LOW->HIGH: 60.0 / 47.5 L invalid"));
  CHECK(!f.ctrl.setPresetVolumeTotal(0.5f));
  CHECK_NEAR(f.hal.total_tank_volume, 47.5f, 1e-6);
  // busy: refused
  f.force_state(ControllerState::FEED_FILLING);
  CHECK(!f.ctrl.setPresetVolumeTotal(50.0f));
  CHECK(contains(f.ctrl.lastRefusalText(), "operation running (FEED_FILLING)"));
  CHECK_NEAR(f.hal.total_tank_volume, 47.5f, 1e-6);
}

TEST(preset_edit, function_off_turns_its_actuators_off_and_blocks_them) {
  ControllerFixture f;
  f.ctrl.setupPresets();
  f.force_state(ControllerState::IDLE);
  CHECK(f.ctrl.commandActuator("NutrientPumpB", true));
  CHECK(f.ctrl.setPresetFunction(FN_NUTRIENTS, false));
  CHECK_EQ(f.hal.count("pump", "NutrientPumpB", false), 1);
  CHECK(!f.gate.isActuatorAllowed("NutrientPumpA"));
  CHECK(!f.ctrl.hasFunction(FN_NUTRIENTS));
  CHECK(f.ctrl.setPresetFunction(FN_NUTRIENTS, true));
  CHECK(f.gate.isActuatorAllowed("NutrientPumpA"));
  CHECK(!f.ctrl.setPresetFunction(FN_NUTRIENTS | FN_DRAIN, false));  // one bit at a time
  f.force_state(ControllerState::PH_MEASURING);
  CHECK(!f.ctrl.setPresetFunction(FN_DRAIN, false));
  CHECK(f.ctrl.hasFunction(FN_DRAIN));
  CHECK(contains(f.ctrl.lastRefusalText(), "Function Drain: operation running"));
}

TEST(preset_edit, restart_grow_only_without_an_operation) {
  ControllerFixture f;
  f.set_time(2026, 11, 14, 18, 0);
  f.ctrl.setupPresets();
  f.cal.sync_to_date(f.ctrl.getLocalDateOrdinal());
  f.cal.set_current_day(50);
  f.force_state(ControllerState::EC_MIXING);
  CHECK(!f.ctrl.restartGrow());
  CHECK_EQ(f.cal.get_current_day(), 50);
  f.force_state(ControllerState::PAUSE);
  CHECK(f.ctrl.restartGrow());
  CHECK_EQ(f.cal.get_current_day(), 1);
}

// ---------------------------------------------------------------------------
// Texts
// ---------------------------------------------------------------------------

TEST(preset_text, status_and_grow_day) {
  ControllerFixture f;
  f.ctrl.setupPresets();
  f.cal.set_current_day(1);
  auto s = f.cal.get_today_schedule();
  char expected[128];
  snprintf(expected, sizeof(expected), "Haupt \xC2\xB7 Day 1/120 \xC2\xB7 %s (pH %.1f-%.1f, ", f.cal.get_phase_name(),
           s.target_ph_min, s.target_ph_max);
  CHECK(contains(f.ctrl.presetStatusText(), expected));
  CHECK_EQ(f.ctrl.growDayText(), std::string("Day 1 / 120"));
  f.force_state(ControllerState::IDLE);
  f.ctrl.requestPreset(MUTTER);
  CHECK(contains(f.ctrl.presetStatusText(), "Mutter \xC2\xB7 Day 1"));
  CHECK_EQ(f.ctrl.tankVolumeText(), std::string("Delta: 10.0L (LOW→HIGH) | Total: 15.0L (EMPTY→HIGH)"));
}
