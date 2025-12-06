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
     * @return true if calibration started successfully
     */
    virtual bool startPhCalibration(float calibrationPoint) = 0;

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

    // HAL interface implementation
    void setPump(const std::string& pumpId, bool state) override;
    void setValve(const std::string& valveId, bool state) override;
    bool getPumpState(const std::string& pumpId) const override;
    bool getValveState(const std::string& valveId) const override;

    float readPH() override;
    bool hasPhValue() const override;
    void onPhChange(std::function<void(float)> callback) override;
    bool startPhCalibration(float calibrationPoint) override;

    float readWaterLevel() override;
    bool hasWaterLevel() const override;

    void setSystemLED(float r, float g, float b, float brightness = 1.0f) override;
    void turnOffLED() override;
    bool isLEDOn() const override;

    uint32_t getSystemTime() const override;

private:
    // Hardware component references (injected via Python)
    esphome::light::LightState* led_{nullptr};
    bool led_is_on_{false};

    esphome::sensor::Sensor* ph_sensor_{nullptr};

    // Actuator state tracking (for getPumpState/getValveState)
    std::map<std::string, bool> pump_states_;
    std::map<std::string, bool> valve_states_;

    // TODO: Add GPIO/PWM output components for pumps/valves (Phase 2)
};

} // namespace plantos_hal
