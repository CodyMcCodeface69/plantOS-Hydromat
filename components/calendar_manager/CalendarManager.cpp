#include "CalendarManager.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace calendar_manager {

static const char* TAG = "calendar_manager";

CalendarManager::CalendarManager() {}

void CalendarManager::setup() {
    ESP_LOGI(TAG, "Initializing CalendarManager...");

    // Initialize NVS preference
    this->pref_ = global_preferences->make_preference<uint8_t>(
        fnv1_hash(NVS_KEY)
    );

    // Load current day from NVS
    this->current_day_ = this->load_current_day();
    ESP_LOGI(TAG, "Current day loaded from NVS: %d", this->current_day_);

    // Parse the schedule JSON
    if (!this->parse_schedule_json()) {
        ESP_LOGE(TAG, "Failed to parse schedule JSON - using default values");
    } else {
        ESP_LOGI(TAG, "Successfully parsed %d days from schedule JSON",
                 this->schedule_map_.size());
    }

    // Log safe mode status
    if (this->safe_mode_) {
        ESP_LOGW(TAG, "SAFE MODE ENABLED - Automated routines disabled");
    }

    // Log verbose mode status
    if (this->verbose_) {
        ESP_LOGI(TAG, "Verbose mode enabled - detailed logging active");
    }

    // Log current schedule
    DailySchedule today = this->get_today_schedule();
    ESP_LOGI(TAG, "Day %d schedule: pH %.2f-%.2f, EC %.0f±%.0f uS/cm, A: %.2f mL/L, B: %.2f mL/L, C: %.2f mL/L, Light: %02d:%02d ON / %02d:%02d OFF",
             today.day_number,
             today.target_ph_min,
             today.target_ph_max,
             today.ec_target,
             today.ec_tolerance,
             today.nutrient_A_ml_per_liter,
             today.nutrient_B_ml_per_liter,
             today.nutrient_C_ml_per_liter,
             today.light_on_time / 60, today.light_on_time % 60,
             today.light_off_time / 60, today.light_off_time % 60);

    ESP_LOGI(TAG, "CalendarManager initialized successfully");
}

void CalendarManager::loop() {
    // Periodic status logging
    uint32_t now = millis();
    if (now - this->last_status_log_time_ >= this->status_log_interval_) {
        this->last_status_log_time_ = now;
        this->log_status();
    }
}

bool CalendarManager::parse_schedule_json() {
    if (this->schedule_json_.empty()) {
        ESP_LOGE(TAG, "Schedule JSON is empty");
        return false;
    }

    if (this->verbose_) {
        ESP_LOGD(TAG, "Parsing schedule JSON (%d bytes)...", this->schedule_json_.length());
    }

    // Use ArduinoJson to parse the JSON string
    // Calculate required capacity (estimate: 120 days * ~100 bytes/entry)
    // Each object now has 10 fields: day, ph_min, ph_max, dose_A/B/C_ml_per_L, light_on_time, light_off_time, ec_target, ec_tolerance
    const size_t capacity = JSON_ARRAY_SIZE(120) + 120 * JSON_OBJECT_SIZE(10) + 3072;
    DynamicJsonDocument doc(capacity);

    DeserializationError error = deserializeJson(doc, this->schedule_json_);
    if (error) {
        ESP_LOGE(TAG, "JSON parsing failed: %s", error.c_str());
        return false;
    }

    // Verify it's an array
    if (!doc.is<JsonArray>()) {
        ESP_LOGE(TAG, "JSON root is not an array");
        return false;
    }

    JsonArray array = doc.as<JsonArray>();
    int parsed_count = 0;

    // Parse each day's schedule
    for (JsonObject obj : array) {
        // Extract fields with defaults
        uint8_t day = obj["day"] | 0;
        float ph_min = obj["ph_min"] | 5.8f;
        float ph_max = obj["ph_max"] | 6.2f;
        float dose_a = obj["dose_A_ml_per_L"] | 0.0f;
        float dose_b = obj["dose_B_ml_per_L"] | 0.0f;
        float dose_c = obj["dose_C_ml_per_L"] | 0.0f;
        uint16_t light_on = obj["light_on_time"] | 960;   // Default: 16:00
        uint16_t light_off = obj["light_off_time"] | 480;  // Default: 08:00
        float ec_target = (obj["ec_target"] | 0.0f) * 1000.0f;      // mS/cm (JSON) → µS/cm (internal)
        float ec_tolerance = (obj["ec_tolerance"] | 0.20f) * 1000.0f;  // mS/cm (JSON) → µS/cm (internal)

        // Validate day number
        if (day < 1 || day > 120) {
            ESP_LOGW(TAG, "Invalid day number %d - skipping", day);
            continue;
        }

        // Store in map
        this->schedule_map_[day] = DailySchedule(day, ph_min, ph_max, dose_a, dose_b, dose_c, light_on, light_off, ec_target, ec_tolerance);
        parsed_count++;

        if (this->verbose_) {
            ESP_LOGD(TAG, "Day %d: pH %.2f-%.2f, EC %.0f±%.0f uS/cm, A:%.2f mL/L B:%.2f mL/L C:%.2f mL/L, Light: %02d:%02d ON / %02d:%02d OFF",
                     day, ph_min, ph_max, ec_target, ec_tolerance, dose_a, dose_b, dose_c,
                     light_on / 60, light_on % 60, light_off / 60, light_off % 60);
        }
    }

    ESP_LOGI(TAG, "Parsed %d days from JSON", parsed_count);
    return parsed_count > 0;
}

DailySchedule CalendarManager::get_schedule(uint8_t day) const {
    auto it = this->schedule_map_.find(day);
    if (it != this->schedule_map_.end()) {
        return it->second;
    }

    // Return default schedule if day not found
    ESP_LOGW(TAG, "Schedule for day %d not found - using defaults", day);
    return DailySchedule(day, 5.8, 6.2, 0.0f, 0.0f, 0.0f);
}

bool CalendarManager::set_current_day(uint8_t day) {
    if (day < 1 || day > 120) {
        ESP_LOGE(TAG, "Invalid day number %d (must be 1-120)", day);
        return false;
    }

    this->current_day_ = day;

    if (this->verbose_) {
        ESP_LOGI(TAG, "Current day set to %d", day);
    }

    return this->save_current_day();
}

bool CalendarManager::advance_day() {
    uint8_t next_day = this->current_day_ + 1;
    if (next_day > 120) {
        next_day = 1;  // Wrap around to day 1
        ESP_LOGI(TAG, "Cycle complete - wrapping to day 1");
    }

    if (this->verbose_) {
        ESP_LOGI(TAG, "Advancing from day %d to day %d", this->current_day_, next_day);
    }

    return this->set_current_day(next_day);
}

bool CalendarManager::go_back_day() {
    uint8_t prev_day = this->current_day_ - 1;
    if (prev_day < 1) {
        prev_day = 120;  // Wrap around to day 120
        ESP_LOGI(TAG, "Going back from day 1 - wrapping to day 120");
    }

    if (this->verbose_) {
        ESP_LOGI(TAG, "Going back from day %d to day %d", this->current_day_, prev_day);
    }

    return this->set_current_day(prev_day);
}

bool CalendarManager::reset_to_day_1() {
    ESP_LOGI(TAG, "Resetting to day 1");
    return this->set_current_day(1);
}

bool CalendarManager::save_current_day() {
    if (this->pref_.save(&this->current_day_)) {
        if (this->verbose_) {
            ESP_LOGD(TAG, "Current day %d saved to NVS", this->current_day_);
        }
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to save current day to NVS");
        return false;
    }
}

uint8_t CalendarManager::load_current_day() {
    uint8_t loaded_day = 1;  // Default to day 1

    if (this->pref_.load(&loaded_day)) {
        // Validate loaded day
        if (loaded_day < 1 || loaded_day > 120) {
            ESP_LOGW(TAG, "Invalid day %d loaded from NVS - resetting to day 1", loaded_day);
            loaded_day = 1;
            this->save_current_day();
        } else {
            ESP_LOGD(TAG, "Loaded day %d from NVS", loaded_day);
        }
    } else {
        ESP_LOGI(TAG, "No saved day in NVS - starting at day 1");
        this->save_current_day();
    }

    return loaded_day;
}

void CalendarManager::log_status() {
    if (!this->verbose_) {
        return;  // Only log if verbose mode is enabled
    }

    DailySchedule today = this->get_today_schedule();

    ESP_LOGI(TAG, "=== CalendarManager Status ===");
    ESP_LOGI(TAG, "Current Day: %d/120", this->current_day_);
    ESP_LOGI(TAG, "Safe Mode: %s", this->safe_mode_ ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "Target pH Range: %.2f - %.2f", today.target_ph_min, today.target_ph_max);
    ESP_LOGI(TAG, "Target EC: %.0f uS/cm (±%.0f)", today.ec_target, today.ec_tolerance);
    ESP_LOGI(TAG, "Nutrient A: %.2f mL/L", today.nutrient_A_ml_per_liter);
    ESP_LOGI(TAG, "Nutrient B: %.2f mL/L", today.nutrient_B_ml_per_liter);
    ESP_LOGI(TAG, "Nutrient C: %.2f mL/L", today.nutrient_C_ml_per_liter);
    ESP_LOGI(TAG, "Light Schedule: %02d:%02d ON / %02d:%02d OFF",
             today.light_on_time / 60, today.light_on_time % 60,
             today.light_off_time / 60, today.light_off_time % 60);
    ESP_LOGI(TAG, "==============================");
}

} // namespace calendar_manager
} // namespace esphome
