// Runtime presets (Anzucht / Mutter / Haupt): boot, capability table, switching, the UI-facing
// preset calls and the "Last Refusal" text. Part of PlantOSController (declared in controller.h).
// The store is GrowProfile (grow_profile.*), the pure model preset_model.h.
// FSMINFO.md "Presets" is authoritative for the behavior.
#include "controller.h"
#include "esphome/components/plantos_hal/hal.h"
#include "esphome/components/actuator_safety_gate/ActuatorSafetyGate.h"
#include "esphome/components/calendar_manager/CalendarManager.h"
#include "esphome/components/time/real_time_clock.h"
#include <cstdio>
#include <cstring>

namespace plantos_controller {

using namespace preset_model;

namespace {
// Actuators of each function (AcidPump belongs to none: pH is always available)
struct FnActuator {
    uint8_t fn;
    const char *id;
};
const FnActuator FN_ACTUATORS[] = {
    {FN_NUTRIENTS, "NutrientPumpA"}, {FN_NUTRIENTS, "NutrientPumpB"}, {FN_NUTRIENTS, "NutrientPumpC"},
    {FN_WATER_FILL, "WaterValve"},   {FN_DRAIN, "WastewaterPump"},    {FN_AIR_PUMP, "AirPump"},
    {FN_GROW_LIGHT, "GrowLight"},
};
}  // namespace

// ============================================================================
// Boot
// ============================================================================

void PlantOSController::setupPresets() {
    profile_.load();
    // Per-day markers are per preset: read lazily from the active preset's keys
    last_auto_feed_day_ = -1;
    last_auto_res_week_ = -1;
    applyPresetToHardware();
    char fns[96];
    ESP_LOGI(TAG, "Preset %s: tank %.1f L (LOW->HIGH) / %.1f L (EMPTY->HIGH), functions %s%s", profile_.active_name(),
             profile_.current().delta_l, profile_.current().total_l,
             formatFunctions(fns, sizeof(fns), profile_.current().mask),
             profile_.is_stored(profile_.active()) ? "" : " (YAML defaults)");
}

void PlantOSController::applyPresetToHardware() {
    const PresetValues &v = profile_.current();
    if (hal_) {
        hal_->setTankVolumeDelta(v.delta_l);
        hal_->setTotalTankVolume(v.total_l);
    }
    if (safety_gate_) {
        for (const auto &a : FN_ACTUATORS) {
            safety_gate_->setActuatorBlocked(a.id, (v.mask & a.fn) == 0);
        }
    }
    if (calendar_manager_) {
        calendar_manager_->set_active_calendar(profile_.active());
    }
}

// ============================================================================
// Capability table: which functions an operation needs (the one place)
// ============================================================================

uint8_t PlantOSController::operationFunctions(Operation op) {
    switch (op) {
        case Operation::FEEDING:
        case Operation::MOTHER_MIX:
        case Operation::EC_CHECK:
            return FN_NUTRIENTS;
        case Operation::FILL:
            return FN_WATER_FILL;
        case Operation::DRAIN:
            return FN_DRAIN;
        case Operation::FEED:  // fill + nutrients (+ pH)
            return FN_WATER_FILL | FN_NUTRIENTS;
        case Operation::RESERVOIR_CHANGE:  // drain + fill + nutrients (+ pH)
            return FN_DRAIN | FN_WATER_FILL | FN_NUTRIENTS;
        case Operation::PH_CORRECTION:
        case Operation::PH_CALIBRATION:
        case Operation::EC_CALIBRATION:
        default:
            return 0;
    }
}

bool PlantOSController::operationAvailable(Operation op) const {
    return missingFunctions(operationFunctions(op), profile_.current().mask) == 0;
}

const char *PlantOSController::missingFunctionsReason(uint8_t missing) const {
    char fns[64];
    snprintf(refusal_reason_, sizeof(refusal_reason_), "not available in preset %s (needs %s)", profile_.active_name(),
             formatFunctions(fns, sizeof(fns), missing));
    return refusal_reason_;
}

bool PlantOSController::actuatorAvailable(const char *actuator_id, const char *what) {
    uint8_t missing = missingFunctions(actuatorFunction(actuator_id), profile_.current().mask);
    if (missing == 0) return true;
    refuse(what, missingFunctionsReason(missing));
    return false;
}

uint32_t PlantOSController::applyNoAirSettle(uint32_t mix_ms) const {
    if (profile_.has(FN_AIR_PUMP)) return mix_ms;
    float f = no_air_settle_factor_ < 1.0f ? 1.0f : no_air_settle_factor_;
    float ms = static_cast<float>(mix_ms) * f;
    float cap = static_cast<float>(mix_ms > NO_AIR_MAX_MIX_MS ? mix_ms : NO_AIR_MAX_MIX_MS);
    return static_cast<uint32_t>(ms > cap ? cap : ms);
}

// ============================================================================
// Refusals
// ============================================================================

void PlantOSController::refuse(const char *what, const char *reason) {
    ESP_LOGW(TAG, "Refused %s: %s (state %s)", what, reason, stateName(current_state_));
    recordRefusal(what, reason);
}

void PlantOSController::recordRefusal(const char *what, const char *reason) {
    char buf[160];
    char clock[8] = "";
    if (time_source_) {
        auto now = time_source_->now();
        if (now.is_valid()) snprintf(clock, sizeof(clock), "%02d:%02d ", now.hour, now.minute);
    }
    snprintf(buf, sizeof(buf), "%s%s: %s", clock, what, reason);
    last_refusal_ = buf;
}

std::string PlantOSController::lastRefusalText() const {
    return last_refusal_.empty() ? std::string("None") : last_refusal_;
}

bool PlantOSController::presetChangeAllowed(const char *what) {
    switch (current_state_) {
        case ControllerState::IDLE:
        case ControllerState::NIGHT:
        case ControllerState::SHUTDOWN:
        case ControllerState::PAUSE:
        case ControllerState::ERROR:
            return true;
        default:
            break;
    }
    snprintf(refusal_reason_, sizeof(refusal_reason_), "operation running (%s)", stateName(current_state_));
    refuse(what, refusal_reason_);
    return false;
}

// ============================================================================
// Actuators of a function
// ============================================================================

void PlantOSController::stopActuatorIfOn(const char *id) {
    if (!safety_gate_) return;
    if (strcmp(id, AIR_PUMP) == 0) {
        // Same path as turnOffAllPumps(): the gate debounces and tracks the Shelly state itself
        if (!safety_gate_->stopAirPumpSequence(false) && safety_gate_->getState(AIR_PUMP)) {
            requestPump(AIR_PUMP, false);
        }
        return;
    }
    if (safety_gate_->getState(id)) {  // only real changes: no "Debouncing" rejections
        requestPump(id, false);
    }
}

void PlantOSController::stopFunctionActuators(uint8_t mask) {
    if (mask & ~FN_GROW_LIGHT) cancelActuatorTests();
    for (const auto &a : FN_ACTUATORS) {
        if (mask & a.fn) stopActuatorIfOn(a.id);
    }
    if (mask & FN_GROW_LIGHT) {
        grow_light_on_ = false;
    }
}

void PlantOSController::restoreAirPumpMode() {
    // Normal mode (24/7) or cycling, whichever the "Air Pump Cycling" switch stored in the gate
    if (safety_gate_) safety_gate_->enableCycling(AIR_PUMP, safety_gate_->isCyclingEnabled(AIR_PUMP));
}

void PlantOSController::resetTankState() {
    // Everything below describes the tank of the previous preset and must not carry over.
    auto_ph_correction_pending_ = false;  // queued post-fill / post-feed pH check
    auto_ec_check_pending_ = false;       // queued post-fill EC check
    ph_post_fill_stabilize_ = false;
    ph_post_feed_stabilize_ = false;
    resetPhCycle();
    sensor_retry_state_.reset();
    ec_pre_feeding_ = 0.0f;
    ec_total_ml_per_L_ = 0.0f;
    ec_attempt_count_ = 0;
    ec_cycle_water_filled_ = false;
    ec_dose_A_ml_ = ec_dose_B_ml_ = ec_dose_C_ml_ = 0.0f;
    feeding_manual_mode_ = false;
    is_mother_mix_ = false;
    is_reservoir_change_ = false;
    last_ec_feeding_timestamp_ = 0;  // 12 h EC feeding guard belongs to the old tank
    last_ph_plausibility_value_ = 0.0f;
    last_ph_plausibility_time_ = 0;
    last_ec_plausibility_value_ = -1.0f;  // no EC jump alert for the step between two tanks
    last_auto_feed_day_ = -1;             // per preset: reload from the new preset's NVS key
    last_auto_res_week_ = -1;
}

// ============================================================================
// Switching
// ============================================================================

bool PlantOSController::requestPreset(uint8_t index) {
    if (index >= PRESET_COUNT) {
        refuse("Preset", "unknown preset");
        return false;
    }
    if (index == profile_.active()) return true;
    char what[24];
    snprintf(what, sizeof(what), "Preset %s", presetName(index));
    if (!presetChangeAllowed(what)) return false;

    const uint8_t old_index = profile_.active();
    const uint8_t old_mask = profile_.current().mask;
    profile_.set_active(index);  // persisted before anything else happens
    const uint8_t new_mask = profile_.current().mask;
    applyPresetToHardware();     // tank volumes, blocked actuators, calendar

    // Dosing pumps, valve and drain pump off (they are off in every allowed state; this is the
    // guarantee). Air pump and grow light only if the new preset lacks them: if it has them,
    // the normal control keeps them as they are (no needless Shelly toggling).
    stopFunctionActuators(FN_NUTRIENTS | FN_WATER_FILL | FN_DRAIN);
    stopActuatorIfOn(ACID_PUMP);
    stopFunctionActuators(static_cast<uint8_t>(~new_mask) & (FN_AIR_PUMP | FN_GROW_LIGHT));
    if ((new_mask & FN_AIR_PUMP) && !(old_mask & FN_AIR_PUMP)) restoreAirPumpMode();

    resetTankState();
    grow_light_first_run_ = true;  // controlGrowLight() re-evaluates in this loop
    did_initial_date_sync_ = false;
    if (calendar_manager_) {
        int32_t today = getLocalDateOrdinal();
        if (today > 0) calendar_manager_->sync_to_date(today);  // the new calendar catches up by date
    }

    char fns[96];
    ESP_LOGI(TAG, "Preset %s -> %s: tank %.1f / %.1f L, functions %s, day %u", presetName(old_index),
             profile_.active_name(), profile_.current().delta_l, profile_.current().total_l,
             formatFunctions(fns, sizeof(fns), new_mask),
             calendar_manager_ ? (unsigned) calendar_manager_->get_current_day() : 0u);
    return true;
}

bool PlantOSController::requestPresetByName(const std::string &name) {
    uint8_t index = presetIndex(name.c_str());
    return requestPreset(index);
}

// ============================================================================
// Editing the active preset
// ============================================================================

bool PlantOSController::setPresetFunction(uint8_t fn, bool on) {
    if (fn == 0 || (fn & ~FN_ALL) != 0 || (fn & (fn - 1)) != 0) return false;  // exactly one bit
    PresetValues v = profile_.current();
    if (((v.mask & fn) != 0) == on) return true;
    char what[32];
    snprintf(what, sizeof(what), "Function %s", functionName(fn));
    if (!presetChangeAllowed(what)) return false;

    v.mask = on ? static_cast<uint8_t>(v.mask | fn) : static_cast<uint8_t>(v.mask & ~fn);
    profile_.set_current(v);
    applyPresetToHardware();
    if (!on) {
        stopFunctionActuators(fn);
    } else if (fn == FN_AIR_PUMP) {
        restoreAirPumpMode();
    }
    if (fn == FN_GROW_LIGHT) grow_light_first_run_ = true;
    ESP_LOGI(TAG, "Preset %s: function %s %s", profile_.active_name(), functionName(fn), on ? "ON" : "OFF");
    return true;
}

bool PlantOSController::setPresetVolumes(float delta_l, float total_l, const char *what) {
    PresetValues v = profile_.current();
    if (sameValues(PresetValues{delta_l, total_l, v.mask}, v)) return true;
    if (!presetChangeAllowed(what)) return false;
    if (!volumesValid(delta_l, total_l)) {
        snprintf(refusal_reason_, sizeof(refusal_reason_), "%.1f / %.1f L invalid (%.0f-%.0f L, LOW->HIGH <= EMPTY->HIGH)",
                 delta_l, total_l, VOLUME_MIN_L, VOLUME_MAX_L);
        refuse(what, refusal_reason_);
        return false;
    }
    v.delta_l = delta_l;
    v.total_l = total_l;
    profile_.set_current(v);
    applyPresetToHardware();
    ESP_LOGI(TAG, "Preset %s: tank %.1f L (LOW->HIGH) / %.1f L (EMPTY->HIGH)", profile_.active_name(), delta_l,
             total_l);
    return true;
}

bool PlantOSController::setPresetVolumeDelta(float liters) {
    return setPresetVolumes(liters, profile_.current().total_l, "Tank Volume LOW->HIGH");
}

bool PlantOSController::setPresetVolumeTotal(float liters) {
    return setPresetVolumes(profile_.current().delta_l, liters, "Tank Volume EMPTY->HIGH");
}

bool PlantOSController::restartGrow() {
    if (!calendar_manager_) return false;
    if (!presetChangeAllowed("Restart Grow")) return false;
    return calendar_manager_->restart_grow();
}

// ============================================================================
// Texts for the UI
// ============================================================================

std::string PlantOSController::presetStatusText() const {
    if (!calendar_manager_) return profile_.active_name();
    auto s = calendar_manager_->get_today_schedule();
    char buf[128];
    formatPresetStatus(buf, sizeof(buf), profile_.active_name(), calendar_manager_->get_current_day(),
                       calendar_manager_->get_defined_days(), calendar_manager_->get_phase_name(), s.target_ph_min,
                       s.target_ph_max, s.ec_target);
    return buf;
}

}  // namespace plantos_controller
