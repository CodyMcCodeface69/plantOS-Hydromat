#include "schedule_math.h"

// The expressions below were moved verbatim out of PlantOSController (integer types and
// operand order included), so results are identical to the pre-split firmware.

namespace plantos_controller {
namespace schedule_math {

bool lightShouldBeOn(uint16_t now_min, uint16_t on_min, uint16_t off_min) {
    if (on_min < off_min) {
        // Normal case: ON and OFF within same day (e.g., ON at 08:00, OFF at 16:00)
        return (now_min >= on_min && now_min < off_min);
    }
    // Wraparound case: light is on overnight (e.g., ON at 16:00, OFF at 08:00)
    return (now_min >= on_min || now_min < off_min);
}

bool hourInNightWindow(uint8_t hour, uint8_t start_hour, uint8_t end_hour) {
    if (start_hour > end_hour) {
        // Spans midnight (e.g., 22:00–08:00)
        return (hour >= start_hour) || (hour < end_hour);
    }
    // Same-day range (e.g., 01:00–06:00)
    return (hour >= start_hour) && (hour < end_hour);
}

int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
    y -= (m <= 2);
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = static_cast<uint32_t>(y - era * 400);                  // [0, 399]
    uint32_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;       // [0, 365]
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                 // [0, 146096]
    int32_t days = era * 146097 + static_cast<int32_t>(doe) - 719468;     // days since 1970-01-01

    return days;
}

int32_t growDaysElapsed(int64_t now_ts, int64_t grow_start_ts) {
    int64_t seconds_elapsed = now_ts - grow_start_ts;
    return static_cast<int32_t>(seconds_elapsed / 86400);  // 86400 seconds per day
}

uint8_t growDayFromElapsed(int32_t days_elapsed) {
    return static_cast<uint8_t>((days_elapsed % 120) + 1);
}

int64_t weekNumber(int64_t timestamp) {
    int64_t days_since_epoch = timestamp / 86400;
    int64_t weeks_since_epoch = days_since_epoch / 7;

    return weeks_since_epoch;
}

uint8_t dayOfWeek(int64_t timestamp) {
    // Unix epoch (Jan 1, 1970) was a Thursday (day 4)
    // (days_since_epoch + 4) % 7 gives: 0=Sunday, 1=Monday, ..., 6=Saturday
    int64_t days_since_epoch = timestamp / 86400;
    return (days_since_epoch + 4) % 7;
}

PhCheckSlot phCheckDue(int64_t now_ts, int64_t last_check_ts, uint32_t seconds_since_midnight,
                       uint32_t interval_s) {
    PhCheckSlot r{};

    if (interval_s >= 86400) {
        // MULTI-DAY INTERVAL (≥24 hours)
        // Example: 48h interval → check every 2 days at midnight
        r.multi_day = true;
        int64_t days_since_epoch = now_ts / 86400;
        int64_t last_check_days = last_check_ts / 86400;
        r.interval_days = interval_s / 86400;
        r.days_since_last = days_since_epoch - last_check_days;

        if (r.days_since_last >= r.interval_days) {
            // Enough days have passed - trigger if we're past midnight
            if (seconds_since_midnight >= 60) {  // Wait 1 minute after midnight for clock stability
                r.due = true;
            }
        }
    } else {
        // DAILY INTERVAL (<24 hours)
        // Example: 2h interval → slots at 0:00, 2:00, 4:00, ..., 22:00
        uint32_t current_slot = seconds_since_midnight / interval_s;

        // Calculate timestamp of current slot boundary
        int64_t day_start = now_ts - seconds_since_midnight;
        int64_t current_slot_timestamp = day_start + (current_slot * interval_s);

        r.slot = current_slot;
        r.slots_per_day = 86400 / interval_s;
        r.slot_hour = (current_slot * interval_s) / 3600;
        r.slot_minute = ((current_slot * interval_s) % 3600) / 60;

        // Trigger if we've crossed into a new slot since last check
        if (current_slot_timestamp > last_check_ts) {
            r.due = true;
        }
    }

    return r;
}

}  // namespace schedule_math
}  // namespace plantos_controller
