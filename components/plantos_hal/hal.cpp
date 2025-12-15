#include "hal.h"
#include "esphome/core/log.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/sensor/sensor.h"

namespace plantos_hal {

static const char* TAG = "plantos_hal";

// ============================================================================
// DEPENDENCY INJECTION (called from Python __init__.py)
// ============================================================================

void ESPHomeHAL::set_led(esphome::light::LightState* led) {
    led_ = led;
    ESP_LOGI(TAG, "LED configured");
}

void ESPHomeHAL::set_ph_sensor(esphome::sensor::Sensor* ph_sensor) {
    ph_sensor_ = ph_sensor;
    ESP_LOGI(TAG, "pH sensor configured");
}

void ESPHomeHAL::set_light_sensor(esphome::sensor::Sensor* light_sensor) {
    light_sensor_ = light_sensor;
    ESP_LOGI(TAG, "Light sensor configured");
}

void ESPHomeHAL::set_temperature_sensor(esphome::sensor::Sensor* temperature_sensor) {
    temperature_sensor_ = temperature_sensor;
    ESP_LOGI(TAG, "Temperature sensor configured");
}

// ============================================================================
// COMPONENT LIFECYCLE
// ============================================================================

void ESPHomeHAL::setup() {
    ESP_LOGI(TAG, "PlantOS HAL initialized");

    // Verify critical dependencies
    if (!led_) {
        ESP_LOGW(TAG, "System LED not configured - LED behaviors will be disabled");
    }
    if (!ph_sensor_) {
        ESP_LOGW(TAG, "pH sensor not configured - pH monitoring will be disabled");
    }
    if (!light_sensor_) {
        ESP_LOGW(TAG, "Light sensor not configured - light intensity monitoring will be disabled");
    }
    if (!temperature_sensor_) {
        ESP_LOGW(TAG, "Temperature sensor not configured - temperature monitoring will be disabled");
    }

    // Initialize actuator state tracking
    pump_states_.clear();
    valve_states_.clear();
}

void ESPHomeHAL::loop() {
    // HAL is passive - no active loop processing needed
    // All actions are triggered by Controller/SafetyGate calls
}

// ============================================================================
// ACTUATORS - Called by SafetyGate ONLY
// ============================================================================

void ESPHomeHAL::setPump(const std::string& pumpId, bool state) {
    ESP_LOGI(TAG, "setPump(%s, %s)", pumpId.c_str(), state ? "ON" : "OFF");

    // Update internal state tracking
    pump_states_[pumpId] = state;

    // TODO: Wire to actual GPIO/PWM outputs after SafetyGate integration
    // For now, this is a stub implementation for Phase 1
    // In Phase 2, we'll connect to actual hardware outputs

    // Example future implementation:
    // if (pumpId == "AcidPump" && acid_pump_output_) {
    //     acid_pump_output_->set_state(state);
    // }
}

void ESPHomeHAL::setValve(const std::string& valveId, bool state) {
    ESP_LOGI(TAG, "setValve(%s, %s)", valveId.c_str(), state ? "OPEN" : "CLOSED");

    // Update internal state tracking
    valve_states_[valveId] = state;

    // TODO: Wire to actual GPIO outputs after SafetyGate integration
}

bool ESPHomeHAL::getPumpState(const std::string& pumpId) const {
    auto it = pump_states_.find(pumpId);
    return it != pump_states_.end() ? it->second : false;
}

bool ESPHomeHAL::getValveState(const std::string& valveId) const {
    auto it = valve_states_.find(valveId);
    return it != valve_states_.end() ? it->second : false;
}

// ============================================================================
// SENSORS - Called by Controller
// ============================================================================

float ESPHomeHAL::readPH() {
    if (!ph_sensor_ || !ph_sensor_->has_state()) {
        return 0.0f;
    }
    return ph_sensor_->state;
}

bool ESPHomeHAL::hasPhValue() const {
    return ph_sensor_ && ph_sensor_->has_state();
}

void ESPHomeHAL::onPhChange(std::function<void(float)> callback) {
    if (!ph_sensor_) {
        ESP_LOGW(TAG, "Cannot register pH callback - sensor not configured");
        return;
    }

    // Register ESPHome sensor callback
    ph_sensor_->add_on_state_callback([callback](float value) {
        callback(value);
    });

    ESP_LOGD(TAG, "pH change callback registered");
}

bool ESPHomeHAL::startPhCalibration(float calibrationPoint) {
    ESP_LOGI(TAG, "pH calibration requested at point: %.2f", calibrationPoint);

    // TODO: Implement pH sensor calibration
    // This will depend on the specific pH sensor component (e.g., ezo_ph)
    // For now, return false (not implemented)

    ESP_LOGW(TAG, "pH calibration not yet implemented in HAL");
    return false;
}

float ESPHomeHAL::readWaterLevel() {
    // TODO: Implement when water level sensor is available
    ESP_LOGD(TAG, "Water level sensor not yet implemented");
    return 0.0f;
}

bool ESPHomeHAL::hasWaterLevel() const {
    // TODO: Implement when water level sensor is available
    return false;
}

float ESPHomeHAL::readLightIntensity() {
    if (!light_sensor_ || !light_sensor_->has_state()) {
        return 0.0f;
    }
    return light_sensor_->state;
}

bool ESPHomeHAL::hasLightIntensity() const {
    return light_sensor_ && light_sensor_->has_state();
}

float ESPHomeHAL::readTemperature() {
    if (!temperature_sensor_ || !temperature_sensor_->has_state()) {
        return 0.0f;
    }
    return temperature_sensor_->state;
}

bool ESPHomeHAL::hasTemperature() const {
    return temperature_sensor_ && temperature_sensor_->has_state();
}

void ESPHomeHAL::onTemperatureChange(std::function<void(float)> callback) {
    if (!temperature_sensor_) {
        ESP_LOGW(TAG, "Cannot register temperature callback - sensor not configured");
        return;
    }

    // Register ESPHome sensor callback
    temperature_sensor_->add_on_state_callback([callback](float value) {
        callback(value);
    });

    ESP_LOGD(TAG, "Temperature change callback registered");
}

// ============================================================================
// USER FEEDBACK - Called by Controller (LED behaviors)
// ============================================================================

void ESPHomeHAL::setSystemLED(float r, float g, float b, float brightness) {
    if (!led_) {
        return; // LED not configured, silent fail
    }

    // Validate inputs
    r = std::clamp(r, 0.0f, 1.0f);
    g = std::clamp(g, 0.0f, 1.0f);
    b = std::clamp(b, 0.0f, 1.0f);
    brightness = std::clamp(brightness, 0.0f, 1.0f);

    // Use ESPHome's LightState API
    auto call = led_->make_call();
    call.set_state(brightness > 0.01f);  // Turn on if brightness > 1%
    call.set_brightness(brightness);
    call.set_rgb(r, g, b);
    call.perform();

    // Track LED state
    led_is_on_ = (brightness > 0.01f);
}

void ESPHomeHAL::turnOffLED() {
    if (!led_) {
        return;
    }

    auto call = led_->make_call();
    call.set_state(false);
    call.perform();

    led_is_on_ = false;
}

bool ESPHomeHAL::isLEDOn() const {
    return led_is_on_;
}

// ============================================================================
// SYSTEM - Called by Controller
// ============================================================================

uint32_t ESPHomeHAL::getSystemTime() const {
    return esphome::millis();
}

} // namespace plantos_hal
