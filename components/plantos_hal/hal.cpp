#include "hal.h"
#include "esphome/core/log.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/ezo_ph_uart/ezo_ph_uart.h"

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

void ESPHomeHAL::set_ph_sensor_component(esphome::ezo_ph_uart::EZOPHUARTComponent* ph_sensor_component) {
    ph_sensor_component_ = ph_sensor_component;
    ESP_LOGI(TAG, "pH sensor component configured for calibration and direct readings");
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
// ACTUATOR SWITCH SETTERS (Phase 2: Hardware Control)
// ============================================================================

void ESPHomeHAL::set_mag_valve_switch(esphome::switch_::Switch* sw) {
    mag_valve_switch_ = sw;
    ESP_LOGI(TAG, "Magnetic valve switch configured (GPIO18)");
}

void ESPHomeHAL::set_pump_ph_switch(esphome::switch_::Switch* sw) {
    pump_ph_switch_ = sw;
    ESP_LOGI(TAG, "pH pump switch configured (GPIO19)");
}

void ESPHomeHAL::set_pump_grow_switch(esphome::switch_::Switch* sw) {
    pump_grow_switch_ = sw;
    ESP_LOGI(TAG, "Grow pump switch configured (GPIO20)");
}

void ESPHomeHAL::set_pump_micro_switch(esphome::switch_::Switch* sw) {
    pump_micro_switch_ = sw;
    ESP_LOGI(TAG, "Micro pump switch configured (GPIO21)");
}

void ESPHomeHAL::set_pump_bloom_switch(esphome::switch_::Switch* sw) {
    pump_bloom_switch_ = sw;
    ESP_LOGI(TAG, "Bloom pump switch configured (GPIO22)");
}

void ESPHomeHAL::set_pump_wastewater_switch(esphome::switch_::Switch* sw) {
    pump_wastewater_switch_ = sw;
    ESP_LOGI(TAG, "Wastewater pump switch configured (GPIO23)");
}

void ESPHomeHAL::set_pump_air_switch(esphome::switch_::Switch* sw) {
    pump_air_switch_ = sw;
    ESP_LOGI(TAG, "Air pump switch configured (GPIO11)");
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
    if (!ph_sensor_component_) {
        ESP_LOGW(TAG, "pH sensor component not configured - calibration and direct readings will be disabled");
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

    // Route to appropriate hardware switch based on pump ID
    if (pumpId == "AcidPump") {
        // pH pump (acid dosing) on GPIO19
        if (pump_ph_switch_) {
            if (state) {
                pump_ph_switch_->turn_on();
            } else {
                pump_ph_switch_->turn_off();
            }
        } else {
            ESP_LOGW(TAG, "pH pump switch not configured - cannot control AcidPump");
        }
    }
    else if (pumpId == "NutrientPumpA") {
        // Grow pump on GPIO20
        if (pump_grow_switch_) {
            if (state) {
                pump_grow_switch_->turn_on();
            } else {
                pump_grow_switch_->turn_off();
            }
        } else {
            ESP_LOGW(TAG, "Grow pump switch not configured - cannot control NutrientPumpA");
        }
    }
    else if (pumpId == "NutrientPumpB") {
        // Micro pump on GPIO21
        if (pump_micro_switch_) {
            if (state) {
                pump_micro_switch_->turn_on();
            } else {
                pump_micro_switch_->turn_off();
            }
        } else {
            ESP_LOGW(TAG, "Micro pump switch not configured - cannot control NutrientPumpB");
        }
    }
    else if (pumpId == "NutrientPumpC") {
        // Bloom pump on GPIO22
        if (pump_bloom_switch_) {
            if (state) {
                pump_bloom_switch_->turn_on();
            } else {
                pump_bloom_switch_->turn_off();
            }
        } else {
            ESP_LOGW(TAG, "Bloom pump switch not configured - cannot control NutrientPumpC");
        }
    }
    else if (pumpId == "WastewaterPump") {
        // Wastewater pump on GPIO23
        if (pump_wastewater_switch_) {
            if (state) {
                pump_wastewater_switch_->turn_on();
            } else {
                pump_wastewater_switch_->turn_off();
            }
        } else {
            ESP_LOGW(TAG, "Wastewater pump switch not configured - cannot control WastewaterPump");
        }
    }
    else if (pumpId == "AirPump") {
        // Air pump on GPIO11
        if (pump_air_switch_) {
            if (state) {
                pump_air_switch_->turn_on();
            } else {
                pump_air_switch_->turn_off();
            }
        } else {
            ESP_LOGW(TAG, "Air pump switch not configured - cannot control AirPump");
        }
    }
    else {
        ESP_LOGW(TAG, "Unknown pump ID: %s", pumpId.c_str());
    }
}

void ESPHomeHAL::setValve(const std::string& valveId, bool state) {
    ESP_LOGI(TAG, "setValve(%s, %s)", valveId.c_str(), state ? "OPEN" : "CLOSED");

    // Update internal state tracking
    valve_states_[valveId] = state;

    // Route to appropriate hardware switch based on valve ID
    if (valveId == "WaterValve") {
        // Magnetic valve (fresh water inlet) on GPIO18
        if (mag_valve_switch_) {
            if (state) {
                mag_valve_switch_->turn_on();
            } else {
                mag_valve_switch_->turn_off();
            }
        } else {
            ESP_LOGW(TAG, "Magnetic valve switch not configured - cannot control WaterValve");
        }
    }
    else {
        ESP_LOGW(TAG, "Unknown valve ID: %s", valveId.c_str());
    }
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

bool ESPHomeHAL::startPhCalibration(float calibrationPoint, int calibrationStep) {
    ESP_LOGI(TAG, "pH calibration requested at point: %.2f (step %d)", calibrationPoint, calibrationStep);

    if (!ph_sensor_component_) {
        ESP_LOGE(TAG, "pH sensor component not configured - cannot calibrate");
        return false;
    }

    // Call appropriate calibration method based on step
    switch (calibrationStep) {
        case 0:  // Mid-point calibration
            return ph_sensor_component_->calibrate_mid(calibrationPoint);
        case 1:  // Low-point calibration
            return ph_sensor_component_->calibrate_low(calibrationPoint);
        case 2:  // High-point calibration
            return ph_sensor_component_->calibrate_high(calibrationPoint);
        default:
            ESP_LOGE(TAG, "Invalid calibration step: %d", calibrationStep);
            return false;
    }
}

bool ESPHomeHAL::takeSinglePhReading(float &value) {
    if (!ph_sensor_component_) {
        ESP_LOGW(TAG, "pH sensor component not configured - cannot take reading");
        return false;
    }

    return ph_sensor_component_->take_single_reading(value);
}

float ESPHomeHAL::getLastPhReading() {
    if (!ph_sensor_component_) {
        return 0.0f;
    }

    return ph_sensor_component_->get_last_reading();
}

void ESPHomeHAL::requestPhReading() {
    if (!ph_sensor_component_) {
        ESP_LOGW(TAG, "pH sensor component not configured - cannot request reading");
        return;
    }

    // Trigger the sensor's update cycle
    ph_sensor_component_->update();
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

bool ESPHomeHAL::sendPhTemperatureCompensation(float temperature) {
    if (!ph_sensor_component_) {
        ESP_LOGW(TAG, "pH sensor component not configured - cannot send temperature compensation");
        return false;
    }

    ESP_LOGI(TAG, "Sending temperature compensation to pH sensor: %.1f°C", temperature);
    return ph_sensor_component_->send_temperature_compensation(temperature);
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
