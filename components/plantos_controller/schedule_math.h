#pragma once
// Pure time and schedule arithmetic of the controller: plain C++ only, no ESPHome includes, so
// the host tests (tests/) call these directly. PlantOSController members read the clock and the
// calendar and pass plain numbers in.

#include <cstdint>

namespace plantos_controller {
namespace schedule_math {

/// Grow-light window, all values in minutes since local midnight.
/// on < off: lit in [on, off). Otherwise the window wraps midnight: lit in [on, 24h) and [0, off).
/// on == off therefore means "always on" (isNightModeHours() excludes that case before calling).
bool lightShouldBeOn(uint16_t now_min, uint16_t on_min, uint16_t off_min);

/// Hour-based night window from the YAML fallback: start > end wraps midnight, otherwise
/// [start, end) within one day (start == end is never night).
bool hourInNightWindow(uint8_t hour, uint8_t start_hour, uint8_t end_hour);

/// Days since 1970-01-01 for a proleptic Gregorian civil date (Howard Hinnant's days_from_civil).
int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d);

/// Whole days between the grow start and now (truncates toward zero, so it is 0 for less
/// than one day before the start).
int32_t growDaysElapsed(int64_t now_ts, int64_t grow_start_ts);

/// Grow day 1..120 for a non-negative day count, wrapping after day 120.
uint8_t growDayFromElapsed(int32_t days_elapsed);

/// Weeks since the Unix epoch from a UTC timestamp. Weeks therefore roll over on Thursday
/// 00:00 UTC (known oddity, kept as is).
int64_t weekNumber(int64_t timestamp);

/// Day of week from a UTC timestamp: 0 = Sunday ... 6 = Saturday (UTC day, not local).
uint8_t dayOfWeek(int64_t timestamp);

/// Decision of the clock-based periodic pH check, with the values the caller logs.
struct PhCheckSlot {
    bool due;
    bool multi_day;           ///< interval >= 24 h
    // multi-day intervals
    int64_t days_since_last;  ///< whole epoch days since the last check
    int64_t interval_days;
    // intervals below 24 h: slots aligned to local midnight
    uint32_t slot;            ///< index of the current slot
    uint32_t slots_per_day;
    uint32_t slot_hour;       ///< start of the current slot
    uint32_t slot_minute;
};

/// Is a periodic pH check due?
/// interval >= 24 h: due when at least interval_days epoch days passed since the last check and
/// it is at least 60 s after local midnight.
/// interval < 24 h: due when the start of the current slot (slots of interval_s seconds counted
/// from local midnight) lies after the last check.
/// interval_s must not be 0 (the caller never passes it; it would divide by zero as before).
PhCheckSlot phCheckDue(int64_t now_ts, int64_t last_check_ts, uint32_t seconds_since_midnight,
                       uint32_t interval_s);

}  // namespace schedule_math
}  // namespace plantos_controller
