#include "tds_sensor.h"
#include "esphome/core/log.h"

namespace tds_sensor {

static const char* TAG = "tds_sensor";

void TDSSensor::setup() {
    if (!sensor_source_) {
        ESP_LOGE(TAG, "ADC sensor source not configured!");
        return;
    }

    // Subscribe to ADC sensor callbacks for voltage buffering
    sensor_source_->add_on_state_callback([this](float voltage) {
        this->on_adc_update(voltage);
    });

    // Reserve buffer capacity
    voltage_buffer_.reserve(sample_count_);

    // Publish initial state
    publish_state(0.0f);

    ESP_LOGI(TAG, "EC sensor initialized (sample_count=%d, default_temp=%.1f°C)",
             sample_count_, default_temperature_);

    if (temperature_sensor_) {
        ESP_LOGI(TAG, "Temperature compensation enabled (DS18B20)");
    } else {
        ESP_LOGI(TAG, "Temperature compensation using default %.1f°C", default_temperature_);
    }
}

void TDSSensor::loop() {
    // Deferred logging from ISR-safe callback
    if (log_buffer_full_) {
        ESP_LOGD(TAG, "Voltage buffer full (%d samples), oldest discarded", sample_count_);
        log_buffer_full_ = false;
    }
    if (log_nan_skipped_) {
        ESP_LOGW(TAG, "NaN voltage reading skipped");
        log_nan_skipped_ = false;
    }
    if (log_overvoltage_skipped_) {
        ESP_LOGW(TAG, "Overvoltage reading (>3.3V) skipped");
        log_overvoltage_skipped_ = false;
    }
}

void TDSSensor::on_adc_update(float voltage) {
    // ISR-safe: no logging, no allocation (buffer pre-reserved)
    if (std::isnan(voltage)) {
        log_nan_skipped_ = true;
        return;
    }

    // Skip overvoltage readings (ADC max is 3.3V)
    if (voltage > 3.3f) {
        log_overvoltage_skipped_ = true;
        return;
    }

    // Skip negative readings
    if (voltage < 0.0f) {
        return;
    }

    // Add to buffer, discard oldest if full
    if (static_cast<int>(voltage_buffer_.size()) >= sample_count_) {
        voltage_buffer_.erase(voltage_buffer_.begin());
        log_buffer_full_ = true;
    }

    voltage_buffer_.push_back(voltage);
}

void TDSSensor::update() {
    // Minimum 3 samples required for meaningful median
    if (voltage_buffer_.size() < 3) {
        ESP_LOGD(TAG, "Not enough samples (%zu/%d), waiting...",
                 voltage_buffer_.size(), sample_count_);
        return;
    }

    // Calculate median voltage
    float median_v = calculate_median();

    // Get temperature for compensation
    float temperature = default_temperature_;
    if (temperature_sensor_ && temperature_sensor_->has_state() &&
        !std::isnan(temperature_sensor_->state)) {
        temperature = temperature_sensor_->state;
    }

    // Apply temperature compensation
    float compensated_v = apply_temperature_compensation(median_v, temperature);

    // Convert to EC
    float ec = voltage_to_ec(compensated_v);

    // Clamp to valid range (0-5000 uS/cm)
    ec = std::max(0.0f, std::min(ec, 5000.0f));

    ESP_LOGI(TAG, "EC: %.1f uS/cm (TDS: %.0f ppm) | median_v=%.3fV, comp_v=%.3fV, temp=%.1f°C, samples=%zu",
             ec, ec * 0.5f, median_v, compensated_v, temperature, voltage_buffer_.size());

    // Publish and clear buffer
    publish_state(ec);
    voltage_buffer_.clear();
}

float TDSSensor::calculate_median() {
    // Sort a copy of the buffer
    std::vector<float> sorted = voltage_buffer_;
    std::sort(sorted.begin(), sorted.end());

    size_t n = sorted.size();
    if (n % 2 == 0) {
        // Even number of elements: average the two middle values
        return (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0f;
    } else {
        // Odd number: take the middle value
        return sorted[n / 2];
    }
}

float TDSSensor::apply_temperature_compensation(float voltage, float temperature) {
    // Standard TDS temperature compensation formula
    // Reference temperature: 25°C, coefficient: 0.02 per °C
    float compensation = 1.0f + 0.02f * (temperature - 25.0f);
    if (compensation <= 0.0f) {
        // Safety: prevent division by zero or negative
        compensation = 0.01f;
    }
    return voltage / compensation;
}

float TDSSensor::voltage_to_ec(float voltage) {
    // KS0429 polynomial: EC (uS/cm) = 133.42*V^3 - 255.86*V^2 + 857.39*V
    // This is the standard TDS polynomial WITHOUT the *0.5 TDS conversion factor
    float v2 = voltage * voltage;
    float v3 = v2 * voltage;
    return 133.42f * v3 - 255.86f * v2 + 857.39f * voltage;
}

}  // namespace tds_sensor
