// Scheduling: night mode / grow-light window, local date and grow day, auto-feed and auto-reservoir-change triggers.
// Part of PlantOSController (declared in controller.h). FSMINFO.md is authoritative for FSM behavior.
#include "controller.h"
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
        ESP_LOGW(TAG, "Time source not configured - cannot determine night mode hours");
        return false;
    }

    auto now = time_source_->now();
    if (!now.is_valid()) {
        ESP_LOGW(TAG, "NTP time not synchronized yet - cannot determine night mode hours");
        return false;
    }

    // Phase 6: Use CalendarManager light schedule if available (minute resolution)
    if (calendar_manager_) {
        auto schedule = calendar_manager_->get_today_schedule();
        // light_on_time / light_off_time are minutes from midnight (e.g., 360 = 06:00)
        // Night = light is OFF = outside light_on…light_off window
        if (schedule.light_on_time != schedule.light_off_time) {
            int current_minutes = now.hour * 60 + now.minute;
            bool light_is_on;
            if (schedule.light_on_time < schedule.light_off_time) {
                // Normal day schedule (e.g., 06:00–22:00)
                light_is_on = (current_minutes >= schedule.light_on_time &&
                               current_minutes < schedule.light_off_time);
            } else {
                // Light-on wraps midnight (e.g., 22:00–06:00)
                light_is_on = (current_minutes >= schedule.light_on_time ||
                               current_minutes < schedule.light_off_time);
            }
            return !light_is_on;  // Night = light off
        }
    }

    // Fallback: YAML-configured hour-based night window
    uint8_t current_hour = now.hour;
    if (night_mode_start_hour_ > night_mode_end_hour_) {
        // Spans midnight (e.g., 22:00–08:00)
        return (current_hour >= night_mode_start_hour_) || (current_hour < night_mode_end_hour_);
    } else {
        // Same-day range (e.g., 01:00–06:00)
        return (current_hour >= night_mode_start_hour_) && (current_hour < night_mode_end_hour_);
    }
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

    y -= (m <= 2);
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = static_cast<uint32_t>(y - era * 400);                  // [0, 399]
    uint32_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;       // [0, 365]
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                 // [0, 146096]
    int32_t days = era * 146097 + static_cast<int32_t>(doe) - 719468;     // days since 1970-01-01

    return days;
}

uint8_t PlantOSController::getCurrentGrowDay() {
    // Automatic day calculation if grow start date and time source are configured
    if (grow_start_timestamp_ > 0 && time_source_ != nullptr) {
        // Get current time from NTP
        auto now = time_source_->now();

        // Check if time is valid (NTP synchronized)
        if (now.is_valid()) {
            // Get current Unix timestamp
            int64_t current_timestamp = now.timestamp;

            // Calculate days elapsed since grow start
            int64_t seconds_elapsed = current_timestamp - grow_start_timestamp_;
            int32_t days_elapsed = static_cast<int32_t>(seconds_elapsed / 86400);  // 86400 seconds per day

            // Handle negative elapsed time (clock not set or start date in future)
            if (days_elapsed < 0) {
                ESP_LOGW(TAG, "Grow start date is in the future or clock not synchronized - using day 1");
                return 1;
            }

            // Calculate current day (1-120, wraps around)
            uint8_t current_day = static_cast<uint8_t>((days_elapsed % 120) + 1);

            ESP_LOGD(TAG, "Auto-calculated grow day: %d (elapsed: %d days since start)",
                     current_day, days_elapsed);

            return current_day;
        } else {
            ESP_LOGW(TAG, "NTP time not synchronized yet - falling back to calendar day counter");
        }
    }

    // Fallback: Use calendar manager's manual day counter (NVS-persisted)
    if (calendar_manager_) {
        uint8_t manual_day = calendar_manager_->get_current_day();
        ESP_LOGD(TAG, "Using calendar manager day counter: %d", manual_day);
        return manual_day;
    }

    // Ultimate fallback: Day 1
    ESP_LOGW(TAG, "No grow start date or calendar manager - defaulting to day 1");
    return 1;
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
    // CHECK 6: Not already fed today
    // ========================================================================
    int64_t today = getCurrentDateTimestamp();
    if (today == 0) {
        ESP_LOGW(TAG, "[AUTO-FEEDING] NTP time unavailable - cannot check date");
        return false;
    }

    // Check in-memory cache first
    if (last_auto_feed_date_ == today) {
        return false;
    }

    // ========================================================================
    // CHECK 7: Verify no NVS event for today (power cycle recovery)
    // ========================================================================
    if (psm_) {
        char date_key[32];
        snprintf(date_key, sizeof(date_key), "AUTOFEED_%lld", (long long)today);

        esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
        if (strncmp(event.eventID, date_key, sizeof(event.eventID)) == 0) {
            // Event exists - feeding already triggered today
            ESP_LOGI(TAG, "[AUTO-FEEDING] Already fed today - cached from NVS");
            last_auto_feed_date_ = today;  // Update cache
            return false;
        }
    }

    // All checks passed - trigger feeding
    return true;
}

int64_t PlantOSController::getCurrentDateTimestamp() {
    // Use the LOCAL calendar date (timezone-adjusted) so the daily feeding limit
    // rolls over at local midnight, consistent with the calendar day advance.
    int32_t today_ordinal = getLocalDateOrdinal();

    if (today_ordinal <= 0) {
        ESP_LOGW(TAG, "[AUTO-FEEDING] NTP time unavailable");
        return 0;
    }

    // Return a unique, stable per-local-day key (local days since epoch scaled to
    // a seconds-granularity timestamp). Used only for equality + NVS key purposes.
    return static_cast<int64_t>(today_ordinal) * 86400;
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
    // CHECK 5: Not already done this week
    // ========================================================================
    int64_t current_week = getCurrentWeekNumber();
    if (current_week == 0) {
        ESP_LOGW(TAG, "[AUTO-RES] NTP time unavailable - cannot check week");
        return false;
    }

    // Check in-memory cache first
    if (last_auto_reservoir_change_week_ == current_week) {
        return false;  // Already done this week
    }

    // ========================================================================
    // CHECK 6: Verify no NVS event for this week (power cycle recovery)
    // ========================================================================
    if (psm_) {
        char week_key[32];
        snprintf(week_key, sizeof(week_key), "AUTORES_%lld", (long long)current_week);

        esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
        if (strncmp(event.eventID, week_key, sizeof(event.eventID)) == 0) {
            // Event exists - reservoir change already triggered this week
            ESP_LOGI(TAG, "[AUTO-RES] Already done this week - cached from NVS");
            last_auto_reservoir_change_week_ = current_week;  // Update cache
            return false;
        }
    }

    // All checks passed - trigger reservoir change
    ESP_LOGI(TAG, "[AUTO-RES] All conditions met - triggering reservoir change");
    return true;
}

int64_t PlantOSController::getCurrentWeekNumber() {
    if (!hal_) {
        return 0;
    }

    // Get current Unix timestamp
    int64_t now = hal_->getCurrentTimestamp();

    if (now == 0) {
        ESP_LOGW(TAG, "[AUTO-RES] NTP time unavailable");
        return 0;
    }

    // Calculate week number (weeks since Unix epoch)
    // Unix epoch started on Thursday, so we adjust by 4 days to align week boundaries
    int64_t days_since_epoch = now / 86400;
    int64_t weeks_since_epoch = days_since_epoch / 7;

    return weeks_since_epoch;
}

uint8_t PlantOSController::getCurrentDayOfWeek() {
    if (!hal_) {
        return 255;
    }

    // Get current Unix timestamp
    int64_t now = hal_->getCurrentTimestamp();

    if (now == 0) {
        return 255;
    }

    // Unix epoch (Jan 1, 1970) was a Thursday (day 4)
    // (days_since_epoch + 4) % 7 gives: 0=Sunday, 1=Monday, ..., 6=Saturday
    int64_t days_since_epoch = now / 86400;
    return (days_since_epoch + 4) % 7;
}
} // namespace plantos_controller
