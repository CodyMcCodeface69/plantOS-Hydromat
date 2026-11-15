// Scheduling: night mode / grow-light window, local date and grow day, auto-feed and auto-reservoir-change triggers.
// Part of PlantOSController (declared in controller.h). FSMINFO.md is authoritative for FSM behavior.
#include "controller.h"
#include "schedule_math.h"
#include "esphome/components/plantos_hal/hal.h"
#include "esphome/components/actuator_safety_gate/ActuatorSafetyGate.h"
#include "esphome/components/persistent_state_manager/persistent_state_manager.h"
#include "esphome/components/calendar_manager/CalendarManager.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/alert_service/alert_service.h"
#include <algorithm>
#include <cmath>

namespace plantos_controller {

// ============================================================================
// Night mode and grow day
// ============================================================================

bool PlantOSController::isNightModeHours() const {
    // Check if we're currently within night mode hours.
    // Priority: CalendarManager light schedule (if available) → YAML config fallback.

    if (!time_source_) {
        ESP_LOGD(TAG, "Night mode: no time source");
        return false;
    }

    auto now = time_source_->now();
    if (!now.is_valid()) {
        ESP_LOGD(TAG, "Night mode: time not synced yet");
        return false;
    }

    // Phase 6: Use CalendarManager light schedule if available (minute resolution)
    if (calendar_manager_) {
        auto schedule = calendar_manager_->get_today_schedule();
        // light_on_time / light_off_time are minutes from midnight (e.g., 360 = 06:00)
        // Night = light is OFF = outside light_on…light_off window
        if (schedule.light_on_time != schedule.light_off_time) {
            int current_minutes = now.hour * 60 + now.minute;
            bool light_is_on = schedule_math::lightShouldBeOn(current_minutes, schedule.light_on_time,
                                                              schedule.light_off_time);
            return !light_is_on;  // Night = light off
        }
    }

    // Fallback: YAML-configured hour-based night window
    // (start > end spans midnight, e.g. 22:00–08:00; otherwise same-day range, e.g. 01:00–06:00)
    uint8_t current_hour = now.hour;
    return schedule_math::hourInNightWindow(current_hour, night_mode_start_hour_, night_mode_end_hour_);
}

int32_t PlantOSController::getLocalDateOrdinal() {
    if (time_source_ == nullptr) {
        return 0;
    }

    auto now = time_source_->now();
    if (!now.is_valid()) {
        return 0;
    }

    // now.year / now.month / now.day_of_month are in the configured local
    // timezone (SNTP applies the TZ). Convert the local civil date to a
    // days-since-epoch ordinal using Howard Hinnant's days_from_civil algorithm.
    // This subtracts cleanly across month/year boundaries and is DST-robust.
    int32_t y = static_cast<int32_t>(now.year);
    uint32_t m = now.month;
    uint32_t d = now.day_of_month;

    return schedule_math::daysFromCivil(y, m, d);
}

// ============================================================================
// Automatic feeding trigger
// ============================================================================

bool PlantOSController::shouldTriggerAutoFeeding() {
    // ========================================================================
    // CHECK 1: Auto-feeding must be enabled
    // ========================================================================
    if (!auto_feeding_enabled_) {
        return false;
    }

    // CHECK 1b: The active preset can run a feed (fill + nutrients). A preset with water fill
    // but no nutrient pumps tops up through auto-fill instead (no half feed).
    if (!operationAvailable(Operation::FEED)) {
        return false;
    }

    // ========================================================================
    // CHECK 2: Must be in IDLE state (not busy)
    // ========================================================================
    if (current_state_ != ControllerState::IDLE) {
        return false;
    }

    // ========================================================================
    // CHECK 3: Not in NIGHT mode hours
    // ========================================================================
    if (night_mode_enabled_ && isNightModeHours()) {
        return false;
    }

    // ========================================================================
    // CHECK 4: Water level sensors must be available
    // ========================================================================
    if (!hal_ || !hal_->hasWaterLevelSensors()) {
        return false;
    }

    // ========================================================================
    // CHECK 5: Water level must be LOW (HIGH=OFF, LOW=OFF, EMPTY=ON)
    // ========================================================================
    bool high = hal_->readWaterLevelHigh();
    bool low = hal_->readWaterLevelLow();
    bool empty = hal_->readWaterLevelEmpty();

    bool is_low_level = (!high && !low && empty);
    if (!is_low_level) {
        return false;
    }

    // ========================================================================
    // CHECK 6: Not already fed today (local date, survives reboots via NVS)
    // ========================================================================
    int32_t today = getLocalDateOrdinal();
    if (today <= 0) {
        ESP_LOGD(TAG, "[AUTO-FEEDING] no valid time - cannot check date");
        return false;
    }

    return today != lastAutoFeedDay();
}

// Per-day markers are per preset, so a feed in Haupt does not block Mutter's feed the same day.
// Haupt keeps the keys of the pre-preset firmware (no migration needed).
const char *PlantOSController::autoFeedDayKey() const {
    static const char *const KEYS[preset_model::PRESET_COUNT] = {"AutoFeedDay0", "AutoFeedDay1", "AutoFeedDay"};
    return KEYS[profile_.active()];
}

const char *PlantOSController::autoResWeekKey() const {
    static const char *const KEYS[preset_model::PRESET_COUNT] = {"AutoResWeek0", "AutoResWeek1", "AutoResWeek"};
    return KEYS[profile_.active()];
}

int32_t PlantOSController::lastAutoFeedDay() {
    if (last_auto_feed_day_ < 0) {
        last_auto_feed_day_ = psm_ ? psm_->loadInt32(autoFeedDayKey(), 0) : 0;
    }
    return last_auto_feed_day_;
}

void PlantOSController::markAutoFeedDay(int32_t day) {
    if (day <= 0 || day == lastAutoFeedDay()) {
        return;
    }
    last_auto_feed_day_ = day;
    if (psm_) {
        psm_->saveInt32(autoFeedDayKey(), day);
    }
}

// ============================================================================
// Automatic weekly reservoir change trigger
// ============================================================================

bool PlantOSController::shouldTriggerAutoReservoirChange() {
    // ========================================================================
    // CHECK 1: Auto reservoir change must be enabled
    // ========================================================================
    if (!auto_reservoir_change_enabled_) {
        return false;
    }

    // CHECK 1b: The active preset can run a reservoir change (drain + fill + nutrients)
    if (!operationAvailable(Operation::RESERVOIR_CHANGE)) {
        return false;
    }

    // ========================================================================
    // CHECK 2: Must be in IDLE state (not busy)
    // ========================================================================
    if (current_state_ != ControllerState::IDLE) {
        return false;
    }

    // ========================================================================
    // CHECK 3: Not in NIGHT mode hours
    // ========================================================================
    if (night_mode_enabled_ && isNightModeHours()) {
        return false;
    }

    // ========================================================================
    // CHECK 4: Today must be the configured day of week
    // ========================================================================
    uint8_t today_dow = getCurrentDayOfWeek();
    if (today_dow == 255) {
        return false;  // Time unavailable
    }

    if (today_dow != auto_reservoir_change_day_) {
        return false;  // Not the right day
    }

    // ========================================================================
    // CHECK 5: Not already done this week (survives reboots via NVS)
    // ========================================================================
    int32_t current_week = getCurrentWeekNumber();
    if (current_week == 0) {
        ESP_LOGD(TAG, "[AUTO-RES] no valid time - cannot check week");
        return false;
    }

    if (current_week == lastAutoResWeek()) {
        return false;  // Already done this week
    }

    return true;
}

int32_t PlantOSController::lastAutoResWeek() {
    if (last_auto_res_week_ < 0) {
        last_auto_res_week_ = psm_ ? psm_->loadInt32(autoResWeekKey(), 0) : 0;
    }
    return last_auto_res_week_;
}

void PlantOSController::markAutoResWeek(int32_t week) {
    if (week <= 0 || week == lastAutoResWeek()) {
        return;
    }
    last_auto_res_week_ = week;
    if (psm_) {
        psm_->saveInt32(autoResWeekKey(), week);
    }
}

int32_t PlantOSController::getCurrentWeekNumber() {
    // Local calendar week (same local date source as the feed date and night mode)
    int32_t today = getLocalDateOrdinal();
    return today > 0 ? schedule_math::weekNumber(today) : 0;
}

uint8_t PlantOSController::getCurrentDayOfWeek() {
    // 0=Sunday, 1=Monday, ..., 6=Saturday (local day), 255 = no valid time
    int32_t today = getLocalDateOrdinal();
    return today > 0 ? schedule_math::dayOfWeek(today) : 255;
}
} // namespace plantos_controller
