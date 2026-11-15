#include "CalendarManager.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace calendar_manager {

static const char *const TAG = "calendar_manager";

// NVS keys (hashed with fnv1_hash). Index = calendar index.
static const char *const NVS_KEY_SLOT[CAL_COUNT] = {"calendar_slot_0", "calendar_slot_1", "calendar_slot_2"};
// Pre-preset firmware (one calendar): uint8_t day and int32_t date. Read once for the
// migration into the Haupt slot, never written.
static const char *const LEGACY_NVS_KEY_DAY = "current_day";
static const char *const LEGACY_NVS_KEY_SYNC_DATE = "last_sync_date";

void CalendarManager::setup() {
    if (this->calendars_ == nullptr) {
        ESP_LOGE(TAG, "No calendar tables - using defaults");
    }
    for (uint8_t i = 0; i < CAL_COUNT; i++) {
        this->load_slot_(i);
    }
    if (this->safe_mode_) {
        ESP_LOGW(TAG, "SAFE MODE ENABLED - Automated routines disabled");
    }
    this->setup_done_ = true;
    this->log_today_("Active");
}

void CalendarManager::loop() {
    uint32_t now = millis();
    if (now - this->last_status_log_time_ >= this->status_log_interval_) {
        this->last_status_log_time_ = now;
        this->log_status();
    }
}

// ---------------------------------------------------------------------------
// NVS
// ---------------------------------------------------------------------------

void CalendarManager::load_slot_(uint8_t index) {
    Slot &slot = this->slots_[index];
    slot.pref = global_preferences->make_preference<SlotRecord>(fnv1_hash(NVS_KEY_SLOT[index]));

    SlotRecord rec{};
    if (slot.pref.load(&rec)) {
        if (rec.day >= 1 && rec.day <= MAX_DAY) {
            slot.day = rec.day;
            slot.last_sync_date = rec.last_sync_date;
            return;
        }
        ESP_LOGW(TAG, "Invalid day %u in NVS slot %u - resetting to day 1", rec.day, index);
        slot.day = 1;
        slot.last_sync_date = rec.last_sync_date;
    } else if (index == CAL_HAUPT && this->migrate_legacy_(slot)) {
        // values taken from the old single-calendar keys
    } else {
        slot.day = 1;
        slot.last_sync_date = 0;
    }
    this->save_slot_(index);
}

bool CalendarManager::migrate_legacy_(Slot &slot) {
    ESPPreferenceObject day_pref = global_preferences->make_preference<uint8_t>(fnv1_hash(LEGACY_NVS_KEY_DAY));
    ESPPreferenceObject date_pref = global_preferences->make_preference<int32_t>(fnv1_hash(LEGACY_NVS_KEY_SYNC_DATE));
    uint8_t day = 0;
    int32_t date = 0;
    bool have_day = day_pref.load(&day);
    bool have_date = date_pref.load(&date);
    if (!have_day && !have_date) {
        return false;
    }
    // The old firmware only ever stored 1..120 and reset anything else to 1.
    slot.day = (have_day && day >= 1 && day <= 120) ? day : 1;
    slot.last_sync_date = have_date ? date : 0;
    ESP_LOGI(TAG, "Migrated old calendar state into Haupt: day %u, sync date %d", slot.day,
             static_cast<int>(slot.last_sync_date));
    return true;
}

bool CalendarManager::save_slot_(uint8_t index) {
    const Slot &slot = this->slots_[index];
    SlotRecord rec{slot.day, SLOT_VERSION, slot.last_sync_date};
    if (!this->slots_[index].pref.save(&rec)) {
        ESP_LOGE(TAG, "Failed to save calendar slot %u to NVS", index);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Active calendar
// ---------------------------------------------------------------------------

bool CalendarManager::set_active_calendar(uint8_t index) {
    if (index >= CAL_COUNT) {
        ESP_LOGE(TAG, "Invalid calendar index %u", index);
        return false;
    }
    if (index != this->active_) {
        this->active_ = index;
        if (this->setup_done_) {  // before setup() the day slots are not loaded yet
            this->log_today_("Switched to");
        }
    }
    return true;
}

const char *CalendarManager::get_calendar_name() const {
    const CalendarDef *cal = this->active_def_();
    return cal ? cal->name : "";
}

const char *CalendarManager::get_phase_name() const {
    const CalendarDef *cal = this->active_def_();
    if (cal == nullptr) return "";
    const Phase *p = find_phase(cal->phases, cal->phase_count, this->get_current_day());
    return p ? p->name : "";
}

uint16_t CalendarManager::get_defined_days() const {
    const CalendarDef *cal = this->active_def_();
    return cal ? cal->days : 0;
}

// ---------------------------------------------------------------------------
// Day counter
// ---------------------------------------------------------------------------

bool CalendarManager::set_current_day(uint16_t day) {
    if (day < 1 || day > MAX_DAY) {
        ESP_LOGE(TAG, "Invalid day number %u (must be 1-%u)", day, MAX_DAY);
        return false;
    }
    this->slots_[this->active_].day = day;
    if (this->verbose_) {
        ESP_LOGI(TAG, "Current day set to %u", day);
    }
    return this->save_slot_(this->active_);
}

bool CalendarManager::advance_day() {
    uint16_t day = this->get_current_day();
    if (day >= MAX_DAY) {
        return false;
    }
    return this->set_current_day(day + 1);
}

bool CalendarManager::go_back_day() {
    uint16_t day = this->get_current_day();
    if (day <= 1) {
        ESP_LOGI(TAG, "Already at day 1");
        return false;
    }
    return this->set_current_day(day - 1);
}

bool CalendarManager::reset_to_day_1() {
    ESP_LOGI(TAG, "Resetting to day 1");
    return this->set_current_day(1);
}

bool CalendarManager::restart_grow() {
    Slot &slot = this->slots_[this->active_];
    slot.day = 1;
    slot.last_sync_date = this->last_seen_ordinal_;  // 0 before the first sync: next sync seeds
    ESP_LOGI(TAG, "%s: grow restarted at day 1", this->get_calendar_name());
    return this->save_slot_(this->active_);
}

bool CalendarManager::sync_to_date(int32_t today_ordinal) {
    if (today_ordinal <= 0) {
        return false;
    }
    this->last_seen_ordinal_ = today_ordinal;
    Slot &slot = this->slots_[this->active_];

    // First sync of this calendar: seed the stored date without advancing
    if (slot.last_sync_date == 0) {
        slot.last_sync_date = today_ordinal;
        this->save_slot_(this->active_);
        ESP_LOGI(TAG, "Calendar date sync seeded at ordinal %d (day %u, no advance)",
                 static_cast<int>(today_ordinal), slot.day);
        return false;
    }

    int32_t delta = today_ordinal - slot.last_sync_date;
    if (delta == 0) {
        return false;
    }
    if (delta < 0) {
        ESP_LOGW(TAG, "Calendar date moved backwards (%d days) - resyncing without rewind",
                 static_cast<int>(delta));
        slot.last_sync_date = today_ordinal;
        this->save_slot_(this->active_);
        return false;
    }

    // delta > 0: count on (no wrap; the last phase holds), manual offsets are kept.
    uint16_t old_day = slot.day;
    int64_t next = static_cast<int64_t>(slot.day) + delta;
    slot.day = next > MAX_DAY ? MAX_DAY : static_cast<uint16_t>(next);
    slot.last_sync_date = today_ordinal;
    this->save_slot_(this->active_);
    ESP_LOGI(TAG, "Calendar advanced %d day(s) from real time: day %u -> %u",
             static_cast<int>(delta), old_day, slot.day);
    return true;
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

void CalendarManager::log_today_(const char *prefix) const {
    DailySchedule t = this->get_today_schedule();
    ESP_LOGI(TAG, "%s %s day %u (%s): pH %.2f-%.2f, EC %.0f±%.0f uS/cm, A/B/C %.2f/%.2f/%.2f mL/L, "
             "light %02u:%02u-%02u:%02u",
             prefix, this->get_calendar_name(), t.day_number, this->get_phase_name(), t.target_ph_min,
             t.target_ph_max, t.ec_target, t.ec_tolerance, t.nutrient_A_ml_per_liter, t.nutrient_B_ml_per_liter,
             t.nutrient_C_ml_per_liter, t.light_on_time / 60, t.light_on_time % 60, t.light_off_time / 60,
             t.light_off_time % 60);
}

void CalendarManager::log_status() {
    if (!this->verbose_) {
        return;
    }
    this->log_today_(this->safe_mode_ ? "[safe mode]" : "Status:");
}

} // namespace calendar_manager
} // namespace esphome
