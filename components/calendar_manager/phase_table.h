#pragma once
// Calendar data types and the phase lookup. Plain C++ (no ESPHome includes), so the
// host tests call the lookup directly. The tables themselves are generated at build
// time from the `calendars:` YAML (see phase_tables.py) as `static const` data.

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace calendar_manager {

// Largest grow day the counter reaches (keep in sync with phase_tables.py MAX_DAY).
static constexpr uint16_t MAX_DAY = 9999;

// Calendar indices; the order is fixed by phase_tables.py CALENDAR_KEYS.
enum CalendarIndex : uint8_t { CAL_ANZUCHT = 0, CAL_MUTTER = 1, CAL_HAUPT = 2, CAL_COUNT = 3 };

/**
 * One phase of a calendar: these values apply from `from_day` until the next phase
 * starts; the last phase applies forever. Units as written in the YAML (EC in mS/cm).
 */
struct Phase {
    uint16_t from_day;
    float ph_min;
    float ph_max;
    float dose_a_ml_per_l;
    float dose_b_ml_per_l;
    float dose_c_ml_per_l;
    uint16_t light_on_min;      // minutes since midnight
    uint16_t light_off_min;
    float ec_target_ms;         // mS/cm, 0 = EC feeding off
    float ec_tolerance_ms;      // mS/cm
    const char *name;
};

struct CalendarDef {
    const char *name;           // "Anzucht", "Mutter", "Haupt"
    const Phase *phases;        // sorted by from_day, phases[0].from_day == 1
    uint8_t phase_count;
    uint16_t days;              // planned length for display, 0 = open-ended
};

/**
 * DailySchedule - the values for one day as the controller uses them (EC in µS/cm).
 */
struct DailySchedule {
    uint16_t day_number;                // Day in the grow cycle (1..MAX_DAY)
    float target_ph_min;                // Lower bound for pH target (e.g., 5.8)
    float target_ph_max;                // Upper bound for pH target (e.g., 6.2)
    float nutrient_A_ml_per_liter;      // Nutrient A concentration (mL per liter of tank volume)
    float nutrient_B_ml_per_liter;      // Nutrient B concentration (mL per liter of tank volume)
    float nutrient_C_ml_per_liter;      // Nutrient C concentration (mL per liter of tank volume)
    uint16_t light_on_time;             // Light ON time (minutes since midnight, e.g., 960 = 16:00)
    uint16_t light_off_time;            // Light OFF time (minutes since midnight, e.g., 480 = 08:00)
    float ec_target;                    // Target EC in µS/cm; 0.0 = EC feeding disabled
    float ec_tolerance;                 // Tolerance band in µS/cm
    // Derived: ec_min_trigger = ec_target - ec_tolerance
    //          ec_max_alarm   = ec_target + ec_tolerance * 1.5

    // Defaults (no calendar / day 0): pH 5.8-6.2, no doses, light 16:00-08:00, EC off.
    DailySchedule()
        : DailySchedule(0, 5.8f, 6.2f, 0.0f, 0.0f, 0.0f) {}

    DailySchedule(uint16_t day, float ph_min, float ph_max,
                  float dose_a, float dose_b, float dose_c,
                  uint16_t light_on = 960, uint16_t light_off = 480,
                  float ec_tgt = 0.0f, float ec_tol = 200.0f)
        : day_number(day),
          target_ph_min(ph_min),
          target_ph_max(ph_max),
          nutrient_A_ml_per_liter(dose_a),
          nutrient_B_ml_per_liter(dose_b),
          nutrient_C_ml_per_liter(dose_c),
          light_on_time(light_on),
          light_off_time(light_off),
          ec_target(ec_tgt),
          ec_tolerance(ec_tol) {}
};

/**
 * The phase that applies on `day`: the last phase with from_day <= day.
 * Days past the last phase hold the last phase. nullptr for day < 1 or an empty table.
 */
inline const Phase *find_phase(const Phase *phases, size_t count, uint32_t day) {
    if (phases == nullptr || count == 0 || day < 1) return nullptr;
    const Phase *found = nullptr;
    for (size_t i = 0; i < count; i++) {
        if (phases[i].from_day > day) break;
        found = &phases[i];
    }
    return found;
}

/**
 * The schedule for `day` of `cal` (EC converted mS/cm -> µS/cm). Without a calendar
 * or for day < 1 the defaults of DailySchedule() are returned (with day_number = day).
 */
inline DailySchedule schedule_for_day(const CalendarDef *cal, uint16_t day) {
    const Phase *p = cal ? find_phase(cal->phases, cal->phase_count, day) : nullptr;
    if (p == nullptr) {
        DailySchedule d;
        d.day_number = day;
        return d;
    }
    return DailySchedule(day, p->ph_min, p->ph_max, p->dose_a_ml_per_l, p->dose_b_ml_per_l, p->dose_c_ml_per_l,
                         p->light_on_min, p->light_off_min,
                         p->ec_target_ms * 1000.0f,       // mS/cm (YAML) -> µS/cm (internal)
                         p->ec_tolerance_ms * 1000.0f);
}

}  // namespace calendar_manager
}  // namespace esphome
