#include "time_dummy.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace time_dummy {

void TimeDummy::setup() {
    ESP_LOGI(TAG, "Initializing TimeDummy component...");

    // Record boot time (millis when setup is called)
    boot_millis_ = millis();

    // Validate initial timestamp
    if (initial_timestamp_ <= 0) {
        ESP_LOGE(TAG, "Invalid initial timestamp (%lld) - time not configured!", initial_timestamp_);
        return;
    }

    ESP_LOGI(TAG, "Initial timestamp: %lld (Unix seconds)", initial_timestamp_);

    // Perform initial time read to populate RealTimeClock state
    this->read_time();

    ESP_LOGI(TAG, "TimeDummy initialized - clock is running");
}

void TimeDummy::update() {
    // Update internal time representation
    this->read_time();
}

bool TimeDummy::read_time() {
    // Calculate current timestamp
    int64_t current_timestamp = this->calculate_current_timestamp();

    // Convert Unix timestamp to ESPTime structure
    // ESPTime represents broken-down time (year, month, day, hour, minute, second)
    ESPTime esp_time = ESPTime::from_epoch_local(current_timestamp);

    // Call synchronize_epoch to update RealTimeClock's internal state
    // This sets time_, time_struct_, and has_time_ for us
    this->synchronize_epoch_(current_timestamp);

    return true;
}

int64_t TimeDummy::calculate_current_timestamp() {
    // Get elapsed time since boot (milliseconds)
    uint32_t current_millis = millis();
    uint32_t elapsed_millis = current_millis - boot_millis_;

    // Convert to seconds
    int64_t elapsed_seconds = elapsed_millis / 1000;

    // Calculate current timestamp:
    // current = initial + elapsed + manual_adjustment
    int64_t current_timestamp = initial_timestamp_ + elapsed_seconds + adjustment_offset_;

    return current_timestamp;
}

int64_t TimeDummy::get_current_timestamp() {
    return this->calculate_current_timestamp();
}

void TimeDummy::add_days(int32_t days) {
    // Add days to adjustment offset (86400 seconds per day)
    int64_t seconds = static_cast<int64_t>(days) * 86400;
    adjustment_offset_ += seconds;

    ESP_LOGI(TAG, "Time adjusted: %s%d day%s (offset: %lld sec)",
             days > 0 ? "+" : "", days, days == 1 || days == -1 ? "" : "s",
             adjustment_offset_);

    // Update time immediately
    this->read_time();

    // Log new current time
    auto esp_time = this->now();
    ESP_LOGI(TAG, "New time: %04d-%02d-%02d %02d:%02d:%02d",
             esp_time.year, esp_time.month, esp_time.day_of_month,
             esp_time.hour, esp_time.minute, esp_time.second);
}

void TimeDummy::add_hours(int32_t hours) {
    // Add hours to adjustment offset (3600 seconds per hour)
    int64_t seconds = static_cast<int64_t>(hours) * 3600;
    adjustment_offset_ += seconds;

    ESP_LOGI(TAG, "Time adjusted: %s%d hour%s (offset: %lld sec)",
             hours > 0 ? "+" : "", hours, hours == 1 || hours == -1 ? "" : "s",
             adjustment_offset_);

    // Update time immediately
    this->read_time();

    // Log new current time
    auto esp_time = this->now();
    ESP_LOGI(TAG, "New time: %04d-%02d-%02d %02d:%02d:%02d",
             esp_time.year, esp_time.month, esp_time.day_of_month,
             esp_time.hour, esp_time.minute, esp_time.second);
}

} // namespace time_dummy
} // namespace esphome
