#pragma once

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/core/log.h"
#include <string>
#include <map>
#include <ArduinoJson.h>

namespace esphome {
namespace calendar_manager {

/**
 * DailySchedule - Structure representing a single day's schedule
 *
 * Contains all target values and dosing durations for a specific day
 * in the grow cycle (days 1-120).
 */
struct DailySchedule {
    uint8_t day_number;                 // Current day in the grow cycle (1-120)
    float target_ph_min;                // Lower bound for pH target (e.g., 5.8)
    float target_ph_max;                // Upper bound for pH target (e.g., 6.2)
    float nutrient_A_ml_per_liter;      // Nutrient A concentration (mL per liter of tank volume)
    float nutrient_B_ml_per_liter;      // Nutrient B concentration (mL per liter of tank volume)
    float nutrient_C_ml_per_liter;      // Nutrient C concentration (mL per liter of tank volume)
    uint16_t light_on_time;             // Light ON time (minutes since midnight, e.g., 960 = 16:00)
    uint16_t light_off_time;            // Light OFF time (minutes since midnight, e.g., 480 = 08:00)

    DailySchedule()
        : day_number(0),
          target_ph_min(5.8),
          target_ph_max(6.2),
          nutrient_A_ml_per_liter(0.0f),
          nutrient_B_ml_per_liter(0.0f),
          nutrient_C_ml_per_liter(0.0f),
          light_on_time(960),             // Default: 16:00
          light_off_time(480) {}          // Default: 08:00

    DailySchedule(uint8_t day, float ph_min, float ph_max,
                  float dose_a, float dose_b, float dose_c,
                  uint16_t light_on = 960, uint16_t light_off = 480)
        : day_number(day),
          target_ph_min(ph_min),
          target_ph_max(ph_max),
          nutrient_A_ml_per_liter(dose_a),
          nutrient_B_ml_per_liter(dose_b),
          nutrient_C_ml_per_liter(dose_c),
          light_on_time(light_on),
          light_off_time(light_off) {}
};

/**
 * CalendarManager - Manages the daily schedule and current day for PlantOS
 *
 * ============================================================================
 * PURPOSE
 * ============================================================================
 *
 * This component manages the 120-day grow cycle schedule, storing target pH
 * values and nutrient dosing durations for each day. The current day number
 * is persisted to NVS (Non-Volatile Storage) to survive power cycles.
 *
 * ============================================================================
 * KEY FEATURES
 * ============================================================================
 *
 * 1. JSON SCHEDULE PARSING: Loads the entire 120-day schedule from a JSON
 *    string in the YAML configuration.
 *
 * 2. NVS PERSISTENCE: Current day number is saved to NVS and restored on boot.
 *
 * 3. SAFE MODE: When enabled, disables automated time-based routines (manual
 *    control via API still works).
 *
 * 4. VERBOSE MODE: When enabled, logs every operation to serial console.
 *
 * 5. STATUS LOGGING: Periodically reports current day and schedule to the
 *    CentralStatusLogger.
 *
 * ============================================================================
 * USAGE EXAMPLE
 * ============================================================================
 *
 * CalendarManager calendar;
 *
 * // Initialize and load schedule from JSON
 * calendar.setup();
 *
 * // Get today's schedule
 * DailySchedule today = calendar.get_today_schedule();
 * ESP_LOGI(TAG, "Target pH: %.2f - %.2f", today.target_ph_min, today.target_ph_max);
 *
 * // Advance to next day
 * calendar.advance_day();
 *
 * // Check if in safe mode
 * if (calendar.is_safe_mode()) {
 *     ESP_LOGW(TAG, "Safe mode enabled - automated routines disabled");
 * }
 *
 * ============================================================================
 * JSON FORMAT
 * ============================================================================
 *
 * The schedule_json parameter must contain a JSON array with 120 objects.
 * Nutrient doses are specified in mL per liter of tank volume (mL/L).
 * Actual mL doses are calculated by: dose_mL = dose_mL_per_L × tank_volume_L
 *
 * Example: For a 10L tank with dose_A_ml_per_L = 1.5:
 *   Actual dose = 1.5 mL/L × 10L = 15 mL
 *
 * [
 *   {
 *     "day": 1,
 *     "ph_min": 5.8,
 *     "ph_max": 6.2,
 *     "dose_A_ml_per_L": 1.5,
 *     "dose_B_ml_per_L": 2.0,
 *     "dose_C_ml_per_L": 1.0,
 *     "light_on_time": 960,
 *     "light_off_time": 480
 *   },
 *   ...
 * ]
 *
 * Light times are in minutes since midnight (0-1439):
 *   - 16:00 = 16 * 60 = 960
 *   - 08:00 = 8 * 60 = 480
 *   - 04:00 = 4 * 60 = 240
 */
class CalendarManager : public Component {
public:
    CalendarManager();

    /**
     * ESPHome Component setup() - Initialize NVS and parse schedule JSON
     *
     * Called automatically by ESPHome during component initialization.
     * Loads current day from NVS and parses the schedule JSON string.
     */
    void setup() override;

    /**
     * ESPHome Component loop() - Periodic status logging
     *
     * Called continuously by ESPHome's main event loop.
     * Periodically reports current day and schedule to status logger.
     */
    void loop() override;

    /**
     * Set the schedule JSON string (called from Python generated code)
     *
     * @param json_str JSON array containing the 120-day schedule
     */
    void set_schedule_json(const std::string& json_str) {
        schedule_json_ = json_str;
    }

    /**
     * Set safe mode (called from Python generated code)
     *
     * @param safe_mode If true, disable automated time-based routines
     */
    void set_safe_mode(bool safe_mode) {
        safe_mode_ = safe_mode;
    }

    /**
     * Set verbose mode (called from Python generated code)
     *
     * @param verbose If true, log every operation to serial console
     */
    void set_verbose(bool verbose) {
        verbose_ = verbose;
    }

    /**
     * Set status log interval (called from Python generated code)
     *
     * @param interval_ms Interval in milliseconds between status reports
     */
    void set_status_log_interval(uint32_t interval_ms) {
        status_log_interval_ = interval_ms;
    }

    /**
     * Get the schedule for a specific day
     *
     * @param day Day number (1-120)
     * @return DailySchedule for the specified day (default schedule if day not found)
     */
    DailySchedule get_schedule(uint8_t day) const;

    /**
     * Get the schedule for the current day
     *
     * @return DailySchedule for today
     */
    DailySchedule get_today_schedule() const {
        return get_schedule(current_day_);
    }

    /**
     * Get the current day number
     *
     * @return Current day (1-120)
     */
    uint8_t get_current_day() const {
        return current_day_;
    }

    /**
     * Set the current day number (and save to NVS)
     *
     * @param day Day number (1-120)
     * @return true if successfully saved to NVS
     */
    bool set_current_day(uint8_t day);

    /**
     * Advance to the next day (and save to NVS)
     *
     * Increments current day and wraps around after day 120.
     * @return true if successfully saved to NVS
     */
    bool advance_day();

    /**
     * Go back to the previous day (and save to NVS)
     *
     * Decrements current day and wraps around to day 120 if at day 1.
     * @return true if successfully saved to NVS
     */
    bool go_back_day();

    /**
     * Reset to day 1 (and save to NVS)
     *
     * @return true if successfully saved to NVS
     */
    bool reset_to_day_1();

    /**
     * Check if safe mode is enabled
     *
     * @return true if automated routines are disabled
     */
    bool is_safe_mode() const {
        return safe_mode_;
    }

    /**
     * Set safe mode
     *
     * @param enabled If true, disable automated routines (manual control still works)
     */
    void set_safe_mode_enabled(bool enabled) {
        if (safe_mode_ != enabled) {
            safe_mode_ = enabled;
            ESP_LOGI("calendar_manager", "Safe mode %s", enabled ? "ENABLED" : "DISABLED");
        }
    }

    /**
     * Toggle safe mode on/off
     *
     * Switches between automated and manual-only operation.
     * When enabled: automated time-based triggers are disabled
     * When disabled: automated triggers are re-enabled
     */
    void toggle_safe_mode() {
        safe_mode_ = !safe_mode_;
        ESP_LOGI("calendar_manager", "Safe mode toggled: %s", safe_mode_ ? "ENABLED" : "DISABLED");
    }

    /**
     * Check if verbose mode is enabled
     *
     * @return true if detailed logging is enabled
     */
    bool is_verbose() const {
        return verbose_;
    }

private:
    // Configuration parameters
    std::string schedule_json_;         // JSON string containing the schedule
    bool safe_mode_{false};             // Disable automated routines
    bool verbose_{false};               // Enable detailed logging
    uint32_t status_log_interval_{30000}; // Status log interval (ms)

    // Schedule storage
    std::map<uint8_t, DailySchedule> schedule_map_; // Map of day -> schedule
    uint8_t current_day_{1};            // Current day (1-120)

    // NVS persistence
    ESPPreferenceObject pref_;
    static constexpr const char* NVS_KEY = "current_day";

    // Status logging
    uint32_t last_status_log_time_{0};

    // Helper methods
    bool parse_schedule_json();
    bool save_current_day();
    uint8_t load_current_day();
    void log_status();
};

} // namespace calendar_manager
} // namespace esphome
