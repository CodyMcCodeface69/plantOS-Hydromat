#include "tds_sensor.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace tds_sensor {

static const char* TAG = "tds_sensor";

void TDSSensor::setup() {
    if (!sensor_source_) {
        ESP_LOGE(TAG, "ADC sensor source not configured!");
        return;
    }

    // Load calibration factor from NVS
    calib_pref_ = esphome::global_preferences->make_preference<float>(
        esphome::fnv1_hash(NVS_KEY_EC_CALIB));
    if (!load_calibration_factor_()) {
        calibration_factor_ = 1.0f;
        ESP_LOGI(TAG, "No saved calibration factor - using 1.0 (uncalibrated)");
    } else {
        ESP_LOGI(TAG, "Loaded EC calibration factor: %.4f", calibration_factor_);
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

void TDSSensor::set_calibration_factor(float factor) {
    // Clamp to valid range (matches EC_Sensor_Calibration.md §5)
    factor = std::max(0.5f, std::min(2.0f, factor));
    calibration_factor_ = factor;
    if (save_calibration_factor_()) {
        ESP_LOGI(TAG, "EC calibration factor saved: %.4f", calibration_factor_);
    } else {
        ESP_LOGE(TAG, "Failed to save EC calibration factor to NVS");
    }
}

void TDSSensor::reset_calibration_factor() {
    calibration_factor_ = 1.0f;
    if (save_calibration_factor_()) {
        ESP_LOGI(TAG, "EC calibration factor reset to 1.0");
    } else {
        ESP_LOGE(TAG, "Failed to reset EC calibration factor in NVS");
    }
}

bool TDSSensor::load_calibration_factor_() {
    float loaded = 1.0f;
    if (!calib_pref_.load(&loaded)) {
        return false;
    }
    // Sanity check loaded value
    if (loaded < 0.5f || loaded > 2.0f) {
        ESP_LOGW(TAG, "NVS calibration factor %.4f out of range, using 1.0", loaded);
        return false;
    }
    calibration_factor_ = loaded;
    return true;
}

bool TDSSensor::save_calibration_factor_() {
    return calib_pref_.save(&calibration_factor_);
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

    // Convert to EC and apply calibration factor
    float ec_raw = voltage_to_ec(compensated_v);
    float ec = ec_raw * calibration_factor_;

    // Clamp to valid range (0-5000 uS/cm)
    ec = std::max(0.0f, std::min(ec, 5000.0f));

    ESP_LOGI(TAG, "EC: %.1f uS/cm (TDS: %.0f ppm) | median_v=%.3fV, comp_v=%.3fV, temp=%.1f°C, samples=%zu, calib=%.4f",
             ec, ec * 0.5f, median_v, compensated_v, temperature, voltage_buffer_.size(), calibration_factor_);

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
    // KS0429 polynomial, explicit two-step pipeline:
    //   Step 1: ppm (500-scale) = (133.42*V^3 - 255.86*V^2 + 857.39*V) * 0.5
    //   Step 2: µS/cm = ppm * 2.0  (canonical internal unit)
    // Net math is identical to the original single-step formula; steps are
    // separated here to make the pipeline traceable to reference material.
    float v2 = voltage * voltage;
    float v3 = v2 * voltage;
    float tds_ppm = (133.42f * v3 - 255.86f * v2 + 857.39f * voltage) * 0.5f;  // ppm 500-scale
    return tds_ppm * 2.0f;  // ppm → µS/cm
}

}  // namespace tds_sensor
