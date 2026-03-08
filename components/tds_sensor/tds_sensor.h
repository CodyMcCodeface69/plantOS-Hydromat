#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/preferences.h"
#include <vector>
#include <algorithm>

namespace tds_sensor {

/**
 * TDS/EC Sensor Component
 *
 * Reads raw ADC voltage from a KS0429 TDS Meter V1.0, applies median filtering
 * and temperature compensation, converts to EC (uS/cm), and publishes the result.
 *
 * Signal chain:
 *   ADC voltage → buffer → median filter → temp compensation → polynomial → EC (uS/cm)
 *
 * KS0429 conversion pipeline (explicit steps):
 *   tds_ppm  = (133.42*V^3 - 255.86*V^2 + 857.39*V) * 0.5   // ppm 500-scale
 *   ec_uS_cm = tds_ppm * 2.0                                  // µS/cm (canonical internal unit)
 *
 * Temperature compensation:
 *   compensatedV = voltage / (1.0 + 0.02 * (temperature - 25.0))
 */
class TDSSensor : public esphome::sensor::Sensor, public esphome::PollingComponent {
public:
    TDSSensor() = default;

    void setup() override;
    void loop() override;
    void update() override;

    // Dependency injection (called from Python sensor.py)
    void set_sensor_source(esphome::sensor::Sensor* source) { sensor_source_ = source; }
    void set_temperature_sensor(esphome::sensor::Sensor* temp) { temperature_sensor_ = temp; }
    void set_sample_count(int count) { sample_count_ = count; }
    void set_default_temperature(float temp) { default_temperature_ = temp; }

    // Calibration factor (NVS-persisted scaling multiplier)
    void set_calibration_factor(float factor);
    float get_calibration_factor() const { return calibration_factor_; }
    void reset_calibration_factor();

    static constexpr const char* NVS_KEY_EC_CALIB = "ECCalibFactor";

protected:
    /**
     * Callback for ADC sensor updates - buffers voltage readings.
     * Called from sensor callback context (ISR-safe: no logging).
     */
    void on_adc_update(float voltage);

    /**
     * Calculate median of voltage buffer.
     * @return Median voltage value
     */
    float calculate_median();

    /**
     * Apply temperature compensation to voltage reading.
     * @param voltage Raw median voltage
     * @param temperature Water temperature in Celsius
     * @return Temperature-compensated voltage
     */
    float apply_temperature_compensation(float voltage, float temperature);

    /**
     * Convert compensated voltage to EC using KS0429 polynomial.
     * @param voltage Temperature-compensated voltage
     * @return EC value in uS/cm
     */
    float voltage_to_ec(float voltage);

private:
    static constexpr const char* TAG = "tds_sensor";

    // Source sensors
    esphome::sensor::Sensor* sensor_source_{nullptr};
    esphome::sensor::Sensor* temperature_sensor_{nullptr};

    // Configuration
    int sample_count_{30};
    float default_temperature_{25.0f};

    // Voltage buffer for median filtering
    std::vector<float> voltage_buffer_;

    // Deferred logging flags (ISR-safe)
    bool log_buffer_full_{false};
    bool log_nan_skipped_{false};
    bool log_overvoltage_skipped_{false};

    // Calibration factor (NVS-persisted)
    float calibration_factor_{1.0f};
    esphome::ESPPreferenceObject calib_pref_;
    bool load_calibration_factor_();
    bool save_calibration_factor_();
};

}  // namespace tds_sensor
