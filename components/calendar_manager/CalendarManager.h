#pragma once

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/core/log.h"
#include "phase_table.h"

namespace esphome {
namespace calendar_manager {

/**
 * CalendarManager - grow calendars and grow-day counters for PlantOS
 *
 * Three calendars (Anzucht, Mutter, Haupt), defined in YAML as phase tables and
 * compiled into flash (see phase_tables.py / phase_table.h). One of them is active
 * (default Haupt); every day/schedule method below acts on the active calendar.
 *
 * Each calendar has its own grow day and last-synced date in NVS, so switching
 * calendars and back resumes by date: sync_to_date() advances the active calendar
 * by the days elapsed since that calendar was last synced.
 *
 * After the last phase its values apply forever; the day counter keeps counting
 * (up to MAX_DAY) instead of wrapping.
 *
 * NVS: one 8-byte record per calendar (key "calendar_slot_<n>"). The single
 * uint8 "current_day" + int32 "last_sync_date" of older firmware are migrated into
 * the Haupt slot on first boot and left in place (an old firmware flashed back
 * catches up from them by date).
 */
class CalendarManager : public Component {
public:
    CalendarManager() = default;

    void setup() override;
    void loop() override;

    // ---- configuration (generated code) ----
    void set_calendars(const CalendarDef *calendars) { calendars_ = calendars; }
    void set_safe_mode(bool safe_mode) { safe_mode_ = safe_mode; }
    void set_verbose(bool verbose) { verbose_ = verbose; }
    void set_status_log_interval(uint32_t interval_ms) { status_log_interval_ = interval_ms; }

    // ---- active calendar ----
    /** Select the active calendar (CAL_ANZUCHT / CAL_MUTTER / CAL_HAUPT). Not persisted here. */
    bool set_active_calendar(uint8_t index);
    uint8_t get_active_calendar() const { return active_; }
    /** "Anzucht" / "Mutter" / "Haupt" ("" without tables). */
    const char *get_calendar_name() const;
    /** Name of the phase that applies today ("" if none). */
    const char *get_phase_name() const;
    /** Planned length of the active calendar in days; 0 = open-ended. */
    uint16_t get_defined_days() const;

    // ---- schedule lookup (active calendar) ----
    /** Schedule for `day`; past the last phase the last phase holds; day 0 = defaults. */
    DailySchedule get_schedule(uint16_t day) const { return schedule_for_day(active_def_(), day); }
    DailySchedule get_today_schedule() const { return get_schedule(get_current_day()); }

    // ---- grow day (active calendar) ----
    uint16_t get_current_day() const { return slots_[active_].day; }
    /** Set the day (1..MAX_DAY) and save it. The in-RAM day changes even if the save fails. */
    bool set_current_day(uint16_t day);
    /** Next day (stops at MAX_DAY). */
    bool advance_day();
    /** Previous day (stays at day 1 and returns false there). */
    bool go_back_day();
    bool reset_to_day_1();
    /**
     * Restart the grow: day 1 as of today. The sync date is set to the last date seen
     * by sync_to_date(); before the first sync it is cleared, so the next sync seeds it.
     */
    bool restart_grow();

    /**
     * Sync the active calendar's grow day to the real date (local days since epoch).
     *  - ordinal <= 0: time not valid, ignored.
     *  - first sync of this calendar: seeds the stored date, no advance.
     *  - same day: no-op. Clock moved backwards: resync the stored date, no rewind.
     *  - delta > 0: day += delta (capped at MAX_DAY, no wrap), date stored.
     * @return true if the grow day was advanced
     */
    bool sync_to_date(int32_t today_ordinal);

    // ---- safe mode / verbose ----
    bool is_safe_mode() const { return safe_mode_; }
    void set_safe_mode_enabled(bool enabled) {
        if (safe_mode_ != enabled) {
            safe_mode_ = enabled;
            ESP_LOGI("calendar_manager", "Safe mode %s", enabled ? "ENABLED" : "DISABLED");
        }
    }
    void toggle_safe_mode() {
        safe_mode_ = !safe_mode_;
        ESP_LOGI("calendar_manager", "Safe mode toggled: %s", safe_mode_ ? "ENABLED" : "DISABLED");
    }
    bool is_verbose() const { return verbose_; }

private:
    // Persisted per calendar. Layout is the NVS format: do not reorder.
    struct SlotRecord {
        uint16_t day;
        uint16_t version;       // SLOT_VERSION
        int32_t last_sync_date; // local days since epoch, 0 = not seeded
    };
    static_assert(sizeof(SlotRecord) == 8, "NVS slot record layout changed");
    static constexpr uint16_t SLOT_VERSION = 1;

    struct Slot {
        uint16_t day{1};
        int32_t last_sync_date{0};
        ESPPreferenceObject pref;
    };

    const CalendarDef *calendars_{nullptr};
    uint8_t active_{CAL_HAUPT};
    Slot slots_[CAL_COUNT];
    int32_t last_seen_ordinal_{0};      // last valid date passed to sync_to_date (RAM only)
    bool setup_done_{false};

    bool safe_mode_{false};
    bool verbose_{false};
    uint32_t status_log_interval_{30000};
    uint32_t last_status_log_time_{0};

    const CalendarDef *active_def_() const { return calendars_ ? &calendars_[active_] : nullptr; }
    void load_slot_(uint8_t index);
    bool migrate_legacy_(Slot &slot);
    bool save_slot_(uint8_t index);
    void log_today_(const char *prefix) const;
    void log_status();
};

} // namespace calendar_manager
} // namespace esphome
