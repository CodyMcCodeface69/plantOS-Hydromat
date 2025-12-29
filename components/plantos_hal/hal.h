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
namespace switch_ {
class Switch;
}
namespace output {
class BinaryOutput;
}
namespace ezo_ph_uart {
class EZOPHUARTComponent;
}
}  // namespace esphome

namespace plantos_hal {

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
     * Set pump state (ON/OFF)
     * @param pumpId Pump identifier (e.g., "AcidPump", "NutrientPumpA", "AirPump")
     * @param state true = ON, false = OFF
     */
    virtual void setPump(const std::string& pumpId, bool state) = 0;

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

    // Actuator output setters (Phase 2: Hardware Control)
    // NOTE: Using GPIO outputs instead of template switches to avoid circular dependency
    // NOTE: Air pump removed - future Zigbee implementation
    void set_mag_valve_output(esphome::output::BinaryOutput* output);
    void set_pump_ph_output(esphome::output::BinaryOutput* output);
    void set_pump_grow_output(esphome::output::BinaryOutput* output);
    void set_pump_micro_output(esphome::output::BinaryOutput* output);
    void set_pump_bloom_output(esphome::output::BinaryOutput* output);
    void set_pump_wastewater_output(esphome::output::BinaryOutput* output);

    // Configuration setters
    void set_ph_reading_interval(uint32_t interval_ms) { ph_reading_interval_ms_ = interval_ms; }
    void set_ph_range(float min_ph, float max_ph) { ph_min_ = min_ph; ph_max_ = max_ph; }

    // Configuration getters
    uint32_t get_ph_reading_interval() const { return ph_reading_interval_ms_; }
    float get_ph_min() const { return ph_min_; }
    float get_ph_max() const { return ph_max_; }

    // HAL interface implementation
    void setPump(const std::string& pumpId, bool state) override;
    void setValve(const std::string& valveId, bool state) override;
    bool getPumpState(const std::string& pumpId) const override;
    bool getValveState(const std::string& valveId) const override;

    float readPH() override;
    bool hasPhValue() const override;
    void onPhChange(std::function<void(float)> callback) override;
    bool startPhCalibration(float calibrationPoint, int calibrationStep) override;
    bool takeSinglePhReading(float &value) override;
    float getLastPhReading() override;
    void requestPhReading() override;

    float readWaterLevel() override;
    bool hasWaterLevel() const override;

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

    // Actuator GPIO outputs (Phase 2: Hardware Control - 6 actuators)
    // NOTE: Using BinaryOutput instead of Switch to avoid circular dependency
    // NOTE: Air pump removed - future Zigbee implementation
    esphome::output::BinaryOutput* mag_valve_output_{nullptr};
    esphome::output::BinaryOutput* pump_ph_output_{nullptr};
    esphome::output::BinaryOutput* pump_grow_output_{nullptr};
    esphome::output::BinaryOutput* pump_micro_output_{nullptr};
    esphome::output::BinaryOutput* pump_bloom_output_{nullptr};
    esphome::output::BinaryOutput* pump_wastewater_output_{nullptr};

    // Actuator state tracking (for getPumpState/getValveState)
    std::map<std::string, bool> pump_states_;
    std::map<std::string, bool> valve_states_;

    // Configuration parameters
    uint32_t ph_reading_interval_ms_{7200000};  // Default: 2 hours
    float ph_min_{5.5f};                         // Default: pH 5.5
    float ph_max_{6.5f};                         // Default: pH 6.5
};

} // namespace plantos_hal
