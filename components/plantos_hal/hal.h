#pragma once

#include <string>
#include <functional>
#include <map>
#include "esphome/core/component.h"

// Forward declarations
namespace esphome {
namespace light {
class LightState;
}
namespace sensor {
class Sensor;
}
namespace binary_sensor {
class BinarySensor;
}
namespace switch_ {
class Switch;
}
namespace output {
class FloatOutput;
}
namespace ezo_ph_uart {
class EZOPHUARTComponent;
}
// HTTP request disabled - using BLE instead
// namespace http_request {
// class HttpRequestComponent;
// }
}  // namespace esphome

namespace plantos_hal {

/**
 * Pump Configuration Structure
 *
 * Stores calibration data for each pump to enable accurate dosing
 * by converting mL volumes to time durations.
 */
struct PumpConfig {
    std::string pump_id;          // Pump identifier (e.g., "AcidPump")
    float flow_rate_ml_s;         // Flow rate in mL/second at configured PWM
    float pwm_intensity;          // PWM intensity (0.0-1.0, default 1.0 = 100%)

    PumpConfig()
        : pump_id(""),
          flow_rate_ml_s(1.0f),   // Default: 1 mL/s
          pwm_intensity(1.0f) {}   // Default: 100% PWM

    PumpConfig(const std::string& id, float flow_rate, float pwm)
        : pump_id(id),
          flow_rate_ml_s(flow_rate),
          pwm_intensity(pwm) {}
};

/**
 * Hardware Abstraction Layer (HAL) Interface
 *
 * This interface provides platform-agnostic access to all hardware components.
 * No direct GPIO, I2C, or hardware API calls should exist above this layer.
 *
 * Layer 3 (Foundation): Hardware Abstraction
 * - Actuators: Controlled by SafetyGate (Layer 2)
 * - Sensors: Read by Controller (Layer 1)
 * - User Feedback: LED control by Controller LED behaviors
 * - System: Time management
 */
class HAL {
public:
    virtual ~HAL() = default;

    // ============================================================================
    // ACTUATORS - Called by SafetyGate ONLY
    // ============================================================================

    /**
     * Set pump state (ON/OFF) - Legacy method for backward compatibility
     * @param pumpId Pump identifier (e.g., "AcidPump", "NutrientPumpA", "AirPump")
     * @param state true = ON, false = OFF
     */
    virtual void setPump(const std::string& pumpId, bool state) = 0;

    /**
     * Set pump state with PWM intensity control
     * @param pumpId Pump identifier (e.g., "AcidPump", "NutrientPumpA")
     * @param state true = ON, false = OFF
     * @param pwmIntensity PWM duty cycle (0.0-1.0, where 1.0 = 100%)
     */
    virtual void setPump(const std::string& pumpId, bool state, float pwmIntensity) = 0;

    /**
     * Calculate pump runtime duration for target volume (Pumpflow)
     * @param pumpId Pump identifier
     * @param targetML Target volume in milliliters
     * @return Duration in seconds (target_ml / flow_rate_ml_s)
     */
    virtual float pumpflow(const std::string& pumpId, float targetML) = 0;

    /**
     * Get pump configuration
     * @param pumpId Pump identifier
     * @return Pump configuration (flow_rate_ml_s, pwm_intensity)
     */
    virtual PumpConfig getPumpConfig(const std::string& pumpId) const = 0;

    /**
     * Set pump configuration
     * @param pumpId Pump identifier
     * @param flowRateMLPerSec Flow rate in mL/second at configured PWM
     * @param pwmIntensity PWM intensity (0.0-1.0)
     */
    virtual void setPumpConfig(const std::string& pumpId, float flowRateMLPerSec, float pwmIntensity) = 0;

    /**
     * Set tank volume
     * @param volumeLiters Tank volume in liters (LOW to HIGH sensor range)
     */
    virtual void setTankVolume(float volumeLiters) = 0;

    /**
     * Get tank volume
     * @return Tank volume in liters
     */
    virtual float getTankVolume() const = 0;

    /**
     * Set magnetic valve flow rate
     * @param flowRateMLPerSec Flow rate in mL/second when valve is open
     */
    virtual void setMagValveFlowRate(float flowRateMLPerSec) = 0;

    /**
     * Get magnetic valve flow rate
     * @return Flow rate in mL/second
     */
    virtual float getMagValveFlowRate() const = 0;

    /**
     * Calculate valve open duration for target volume (valveflow)
     * @param targetML Target volume in milliliters
     * @return Duration in seconds (target_ml / mag_valve_flow_rate_ml_s)
     */
    virtual float valveflow(float targetML) = 0;

    /**
     * Set valve state (OPEN/CLOSED)
     * @param valveId Valve identifier (e.g., "WaterValve")
     * @param state true = OPEN, false = CLOSED
     */
    virtual void setValve(const std::string& valveId, bool state) = 0;

    /**
     * Get current pump state
     * @param pumpId Pump identifier
     * @return true if pump is ON, false if OFF
     */
    virtual bool getPumpState(const std::string& pumpId) const = 0;

    /**
     * Get current valve state
     * @param valveId Valve identifier
     * @return true if valve is OPEN, false if CLOSED
     */
    virtual bool getValveState(const std::string& valveId) const = 0;

    /**
     * Check actual Shelly switch status via HTTP (for air pump health monitoring)
     * @param pumpId Pump identifier (e.g., "AirPump")
     * @return true if pump is actually ON according to Shelly, false otherwise
     *
     * This method queries the Shelly device directly via HTTP GET to verify
     * the actual hardware state, not just the tracked state.
     * Used for health monitoring and recovery from manual shutoffs or network failures.
     */
    virtual bool checkShellySwitchStatus(const std::string& pumpId) = 0;

    // ============================================================================
    // SENSORS - Called by Controller
    // ============================================================================

    /**
     * Read current pH value
     * @return pH value (0.0-14.0), or 0.0 if not available
     */
    virtual float readPH() = 0;

    /**
     * Check if pH sensor has valid reading
     * @return true if pH value is available
     */
    virtual bool hasPhValue() const = 0;

    /**
     * Register callback for pH value changes
     * @param callback Function to call when pH value updates
     */
    virtual void onPhChange(std::function<void(float)> callback) = 0;

    /**
     * Start pH sensor calibration at given point
     * @param calibrationPoint Calibration pH value (e.g., 4.0, 7.0, 10.0)
     * @param calibrationStep 0=mid, 1=low, 2=high
     * @return true if calibration command succeeded
     */
    virtual bool startPhCalibration(float calibrationPoint, int calibrationStep) = 0;

    /**
     * Take a single pH reading immediately (blocking)
     * @param value Output parameter for pH value
     * @return true if reading was successful
     */
    virtual bool takeSinglePhReading(float &value) = 0;

    /**
     * Get the last pH reading from sensor
     * @return Most recent pH value
     */
    virtual float getLastPhReading() = 0;

    /**
     * Request pH sensor to take a reading
     * This triggers the sensor's update cycle
     */
    virtual void requestPhReading() = 0;

    /**
     * Get configured pH reading interval in milliseconds
     * @return Reading interval in ms
     */
    virtual uint32_t get_ph_reading_interval() const = 0;

    /**
     * Get configured minimum pH threshold
     * @return Minimum pH value
     */
    virtual float get_ph_min() const = 0;

    /**
     * Get configured maximum pH threshold
     * @return Maximum pH value
     */
    virtual float get_ph_max() const = 0;

    /**
     * Read water level
     * @return Water level (implementation-specific units)
     */
    virtual float readWaterLevel() = 0;

    /**
     * Check if water level sensor has valid reading
     * @return true if water level is available
     */
    virtual bool hasWaterLevel() const = 0;

    /**
     * Read water level HIGH sensor (XKC-Y23-V on GPIO10)
     * @return true if water is at or above HIGH level, false otherwise
     */
    virtual bool readWaterLevelHigh() = 0;

    /**
     * Read water level LOW sensor (XKC-Y23-V on GPIO11)
     * @return true if water is at or above LOW level, false otherwise
     */
    virtual bool readWaterLevelLow() = 0;

    /**
     * Check if water level sensors are configured and available
     * @return true if both HIGH and LOW sensors are available
     */
    virtual bool hasWaterLevelSensors() const = 0;

    /**
     * Read light intensity from analog sensor
     * @return Light intensity raw ADC value (0.0-1.0 normalized, or raw voltage)
     */
    virtual float readLightIntensity() = 0;

    /**
     * Check if light intensity sensor has valid reading
     * @return true if light intensity value is available
     */
    virtual bool hasLightIntensity() const = 0;

    /**
     * Read temperature from DS18B20 sensor
     * @return Temperature in degrees Celsius, or 0.0 if not available
     */
    virtual float readTemperature() = 0;

    /**
     * Check if temperature sensor has valid reading
     * @return true if temperature value is available
     */
    virtual bool hasTemperature() const = 0;

    /**
     * Register callback for temperature value changes
     * @param callback Function to call when temperature value updates
     */
    virtual void onTemperatureChange(std::function<void(float)> callback) = 0;

    /**
     * Send temperature compensation to pH sensor
     * @param temperature Temperature in degrees Celsius
     * @return true if compensation was successfully sent
     */
    virtual bool sendPhTemperatureCompensation(float temperature) = 0;

    // ============================================================================
    // USER FEEDBACK - Called by Controller (LED behaviors)
    // ============================================================================

    /**
     * Set system LED color and brightness
     * @param r Red component (0.0-1.0)
     * @param g Green component (0.0-1.0)
     * @param b Blue component (0.0-1.0)
     * @param brightness Overall brightness (0.0-1.0)
     */
    virtual void setSystemLED(float r, float g, float b, float brightness = 1.0f) = 0;

    /**
     * Turn off system LED
     */
    virtual void turnOffLED() = 0;

    /**
     * Check if LED is currently on
     * @return true if LED is on
     */
    virtual bool isLEDOn() const = 0;

    // ============================================================================
    // SYSTEM - Called by Controller
    // ============================================================================

    /**
     * Get system uptime in milliseconds
     * @return Milliseconds since system boot (equivalent to millis())
     */
    virtual uint32_t getSystemTime() const = 0;
};

/**
 * ESPHome HAL Implementation
 *
 * Wraps ESPHome APIs (light::LightState, sensor::Sensor, GPIO, etc.)
 * to provide a clean hardware abstraction layer.
 */
class ESPHomeHAL : public HAL, public esphome::Component {
public:
    ESPHomeHAL() = default;

    // Component lifecycle
    void setup() override;
    void loop() override;

    // Dependency injection (called from Python __init__.py)
    void set_led(esphome::light::LightState* led);
    void set_ph_sensor(esphome::sensor::Sensor* ph_sensor);
    void set_ph_sensor_component(esphome::ezo_ph_uart::EZOPHUARTComponent* ph_sensor_component);
    void set_light_sensor(esphome::sensor::Sensor* light_sensor);
    void set_temperature_sensor(esphome::sensor::Sensor* temperature_sensor);
    void set_water_level_high_sensor(esphome::binary_sensor::BinarySensor* sensor);
    void set_water_level_low_sensor(esphome::binary_sensor::BinarySensor* sensor);

    // Actuator output setters (Phase 2: Hardware Control)
    // NOTE: Using LEDC PWM outputs for pump control with variable intensity
    void set_mag_valve_output(esphome::output::FloatOutput* output);
    void set_pump_ph_output(esphome::output::FloatOutput* output);
    void set_pump_grow_output(esphome::output::FloatOutput* output);
    void set_pump_micro_output(esphome::output::FloatOutput* output);
    void set_pump_bloom_output(esphome::output::FloatOutput* output);
    void set_pump_wastewater_output(esphome::output::FloatOutput* output);

    // Shelly HTTP switch setters (MVP: AirPump and WastewaterPump via Shelly Plus 4PM)
    void set_air_pump_switch(esphome::switch_::Switch* sw);
    void set_wastewater_pump_switch(esphome::switch_::Switch* sw);

    // Shelly BLE switch setters (Alternative to HTTP for mesh WiFi compatibility)
    // Use these instead of HTTP switches to avoid OTA conflicts
    void set_air_pump_ble_switch(esphome::switch_::Switch* sw);
    void set_wastewater_pump_ble_switch(esphome::switch_::Switch* sw);
    void set_grow_light_ble_switch(esphome::switch_::Switch* sw);

    // HTTP request component setter (DISABLED - using BLE instead)
    // void set_http_request(esphome::http_request::HttpRequestComponent* http);

    // Configuration setters
    void set_ph_reading_interval(uint32_t interval_ms) { ph_reading_interval_ms_ = interval_ms; }
    void set_ph_range(float min_ph, float max_ph) { ph_min_ = min_ph; ph_max_ = max_ph; }

    // Configuration getters
    uint32_t get_ph_reading_interval() const { return ph_reading_interval_ms_; }
    float get_ph_min() const { return ph_min_; }
    float get_ph_max() const { return ph_max_; }

    // HAL interface implementation
    void setPump(const std::string& pumpId, bool state) override;
    void setPump(const std::string& pumpId, bool state, float pwmIntensity) override;
    float pumpflow(const std::string& pumpId, float targetML) override;
    PumpConfig getPumpConfig(const std::string& pumpId) const override;
    void setPumpConfig(const std::string& pumpId, float flowRateMLPerSec, float pwmIntensity) override;
    void setTankVolume(float volumeLiters) override;
    float getTankVolume() const override;
    void setMagValveFlowRate(float flowRateMLPerSec) override;
    float getMagValveFlowRate() const override;
    float valveflow(float targetML) override;
    void setValve(const std::string& valveId, bool state) override;
    bool getPumpState(const std::string& pumpId) const override;
    bool getValveState(const std::string& valveId) const override;
    bool checkShellySwitchStatus(const std::string& pumpId) override;

    float readPH() override;
    bool hasPhValue() const override;
    void onPhChange(std::function<void(float)> callback) override;
    bool startPhCalibration(float calibrationPoint, int calibrationStep) override;
    bool takeSinglePhReading(float &value) override;
    float getLastPhReading() override;
    void requestPhReading() override;

    float readWaterLevel() override;
    bool hasWaterLevel() const override;
    bool readWaterLevelHigh() override;
    bool readWaterLevelLow() override;
    bool hasWaterLevelSensors() const override;

    float readLightIntensity() override;
    bool hasLightIntensity() const override;

    float readTemperature() override;
    bool hasTemperature() const override;
    void onTemperatureChange(std::function<void(float)> callback) override;
    bool sendPhTemperatureCompensation(float temperature) override;

    void setSystemLED(float r, float g, float b, float brightness = 1.0f) override;
    void turnOffLED() override;
    bool isLEDOn() const override;

    uint32_t getSystemTime() const override;

private:
    // Hardware component references (injected via Python)
    esphome::light::LightState* led_{nullptr};
    bool led_is_on_{false};

    esphome::sensor::Sensor* ph_sensor_{nullptr};
    esphome::ezo_ph_uart::EZOPHUARTComponent* ph_sensor_component_{nullptr};
    esphome::sensor::Sensor* light_sensor_{nullptr};
    esphome::sensor::Sensor* temperature_sensor_{nullptr};
    esphome::binary_sensor::BinarySensor* water_level_high_sensor_{nullptr};
    esphome::binary_sensor::BinarySensor* water_level_low_sensor_{nullptr};

    // Actuator GPIO outputs (Phase 2: Hardware Control - 6 actuators)
    // NOTE: Using FloatOutput (LEDC PWM) for variable pump intensity control
    esphome::output::FloatOutput* mag_valve_output_{nullptr};
    esphome::output::FloatOutput* pump_ph_output_{nullptr};
    esphome::output::FloatOutput* pump_grow_output_{nullptr};
    esphome::output::FloatOutput* pump_micro_output_{nullptr};
    esphome::output::FloatOutput* pump_bloom_output_{nullptr};
    esphome::output::FloatOutput* pump_wastewater_output_{nullptr};

    // Shelly HTTP switches (MVP: AirPump and WastewaterPump)
    esphome::switch_::Switch* air_pump_switch_{nullptr};
    esphome::switch_::Switch* wastewater_pump_switch_{nullptr};

    // Shelly BLE switches (Alternative to HTTP - preferred for mesh WiFi)
    esphome::switch_::Switch* air_pump_ble_switch_{nullptr};
    esphome::switch_::Switch* wastewater_pump_ble_switch_{nullptr};
    esphome::switch_::Switch* grow_light_ble_switch_{nullptr};

    // HTTP request component for direct Shelly control (DISABLED - using BLE)
    // esphome::http_request::HttpRequestComponent* http_request_{nullptr};

    // Shelly IP address
    static constexpr const char* SHELLY_IP = "192.168.0.130";

    // Actuator state tracking (for getPumpState/getValveState)
    std::map<std::string, bool> pump_states_;
    std::map<std::string, bool> valve_states_;

    // Pump configuration storage
    std::map<std::string, PumpConfig> pump_configs_;

    // Tank and valve configuration
    float tank_volume_liters_{10.0f};            // Default: 10 liters
    float mag_valve_flow_rate_ml_s_{50.0f};     // Default: 50 mL/s (3 L/min)

    // Configuration parameters
    uint32_t ph_reading_interval_ms_{7200000};  // Default: 2 hours
    float ph_min_{5.5f};                         // Default: pH 5.5
    float ph_max_{6.5f};                         // Default: pH 6.5
};

} // namespace plantos_hal
