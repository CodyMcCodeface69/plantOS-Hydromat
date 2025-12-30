#include "hal.h"
#include "esphome/core/log.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/output/float_output.h"
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

void ESPHomeHAL::set_water_level_high_sensor(esphome::binary_sensor::BinarySensor* sensor) {
    water_level_high_sensor_ = sensor;
    ESP_LOGI(TAG, "Water level HIGH sensor configured");
}

void ESPHomeHAL::set_water_level_low_sensor(esphome::binary_sensor::BinarySensor* sensor) {
    water_level_low_sensor_ = sensor;
    ESP_LOGI(TAG, "Water level LOW sensor configured");
}

// ============================================================================
// ACTUATOR OUTPUT SETTERS (Phase 2: Hardware Control)
// ============================================================================

void ESPHomeHAL::set_mag_valve_output(esphome::output::FloatOutput* output) {
    mag_valve_output_ = output;
    ESP_LOGI(TAG, "Magnetic valve output configured (GPIO18 - PWM capable)");
}

void ESPHomeHAL::set_pump_ph_output(esphome::output::FloatOutput* output) {
    pump_ph_output_ = output;
    ESP_LOGI(TAG, "pH pump output configured (GPIO19 - PWM capable)");
}

void ESPHomeHAL::set_pump_grow_output(esphome::output::FloatOutput* output) {
    pump_grow_output_ = output;
    ESP_LOGI(TAG, "Grow pump output configured (GPIO20 - PWM capable)");
}

void ESPHomeHAL::set_pump_micro_output(esphome::output::FloatOutput* output) {
    pump_micro_output_ = output;
    ESP_LOGI(TAG, "Micro pump output configured (GPIO21 - PWM capable)");
}

void ESPHomeHAL::set_pump_bloom_output(esphome::output::FloatOutput* output) {
    pump_bloom_output_ = output;
    ESP_LOGI(TAG, "Bloom pump output configured (GPIO22 - PWM capable)");
}

void ESPHomeHAL::set_pump_wastewater_output(esphome::output::FloatOutput* output) {
    pump_wastewater_output_ = output;
    ESP_LOGI(TAG, "Wastewater pump output configured (GPIO23 - PWM capable)");
}

// NOTE: set_pump_air_output removed - future Zigbee implementation

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

    // Initialize pump configurations with defaults (will be overridden from YAML)
    // Default: 1.0 mL/s @ 100% PWM for all pumps
    if (pump_configs_.find("AcidPump") == pump_configs_.end()) {
        pump_configs_["AcidPump"] = PumpConfig("AcidPump", 1.0f, 1.0f);
    }
    if (pump_configs_.find("NutrientPumpA") == pump_configs_.end()) {
        pump_configs_["NutrientPumpA"] = PumpConfig("NutrientPumpA", 1.0f, 1.0f);
    }
    if (pump_configs_.find("NutrientPumpB") == pump_configs_.end()) {
        pump_configs_["NutrientPumpB"] = PumpConfig("NutrientPumpB", 1.0f, 1.0f);
    }
    if (pump_configs_.find("NutrientPumpC") == pump_configs_.end()) {
        pump_configs_["NutrientPumpC"] = PumpConfig("NutrientPumpC", 1.0f, 1.0f);
    }

    ESP_LOGI(TAG, "Pump configurations initialized:");
    for (const auto& pair : pump_configs_) {
        ESP_LOGI(TAG, "  %s: %.3f mL/s @ %.0f%% PWM",
                 pair.first.c_str(), pair.second.flow_rate_ml_s, pair.second.pwm_intensity * 100.0f);
    }
}

void ESPHomeHAL::loop() {
    // HAL is passive - no active loop processing needed
    // All actions are triggered by Controller/SafetyGate calls
}

// ============================================================================
// ACTUATORS - Called by SafetyGate ONLY
// ============================================================================

void ESPHomeHAL::setPump(const std::string& pumpId, bool state) {
    // Legacy method - use configured PWM intensity from pump config
    auto it = pump_configs_.find(pumpId);
    float pwm = (it != pump_configs_.end()) ? it->second.pwm_intensity : 1.0f;
    setPump(pumpId, state, pwm);
}

void ESPHomeHAL::setPump(const std::string& pumpId, bool state, float pwmIntensity) {
    ESP_LOGI(TAG, "setPump(%s, %s, PWM=%.0f%%)", pumpId.c_str(), state ? "ON" : "OFF", pwmIntensity * 100.0f);

    // Update internal state tracking
    pump_states_[pumpId] = state;

    // Clamp PWM intensity to valid range
    pwmIntensity = std::clamp(pwmIntensity, 0.0f, 1.0f);

    // Calculate effective duty cycle (OFF = 0%, ON = pwmIntensity)
    float dutyCycle = state ? pwmIntensity : 0.0f;

    // Route to appropriate GPIO output based on pump ID
    if (pumpId == "AcidPump") {
        // pH pump (acid dosing) on GPIO19
        if (pump_ph_output_) {
            pump_ph_output_->set_level(dutyCycle);
        } else {
            ESP_LOGW(TAG, "pH pump output not configured - cannot control AcidPump");
        }
    }
    else if (pumpId == "NutrientPumpA") {
        // Grow pump on GPIO20
        if (pump_grow_output_) {
            pump_grow_output_->set_level(dutyCycle);
        } else {
            ESP_LOGW(TAG, "Grow pump output not configured - cannot control NutrientPumpA");
        }
    }
    else if (pumpId == "NutrientPumpB") {
        // Micro pump on GPIO21
        if (pump_micro_output_) {
            pump_micro_output_->set_level(dutyCycle);
        } else {
            ESP_LOGW(TAG, "Micro pump output not configured - cannot control NutrientPumpB");
        }
    }
    else if (pumpId == "NutrientPumpC") {
        // Bloom pump on GPIO22
        if (pump_bloom_output_) {
            pump_bloom_output_->set_level(dutyCycle);
        } else {
            ESP_LOGW(TAG, "Bloom pump output not configured - cannot control NutrientPumpC");
        }
    }
    else if (pumpId == "WastewaterPump") {
        // Wastewater pump on GPIO23
        if (pump_wastewater_output_) {
            pump_wastewater_output_->set_level(dutyCycle);
        } else {
            ESP_LOGW(TAG, "Wastewater pump output not configured - cannot control WastewaterPump");
        }
    }
    // NOTE: AirPump removed - future Zigbee implementation
    else {
        ESP_LOGW(TAG, "Unknown pump ID: %s", pumpId.c_str());
    }
}

float ESPHomeHAL::pumpflow(const std::string& pumpId, float targetML) {
    auto it = pump_configs_.find(pumpId);
    if (it == pump_configs_.end()) {
        ESP_LOGW(TAG, "pumpflow(%s): Pump not configured, using default 1 mL/s", pumpId.c_str());
        return targetML / 1.0f;  // Default: 1 mL/s
    }

    const PumpConfig& config = it->second;
    if (config.flow_rate_ml_s <= 0.0f) {
        ESP_LOGE(TAG, "pumpflow(%s): Invalid flow rate %.3f mL/s", pumpId.c_str(), config.flow_rate_ml_s);
        return 0.0f;
    }

    float duration_s = targetML / config.flow_rate_ml_s;
    ESP_LOGI(TAG, "pumpflow(%s): %.1f mL @ %.3f mL/s = %.2f seconds",
             pumpId.c_str(), targetML, config.flow_rate_ml_s, duration_s);
    return duration_s;
}

PumpConfig ESPHomeHAL::getPumpConfig(const std::string& pumpId) const {
    auto it = pump_configs_.find(pumpId);
    if (it != pump_configs_.end()) {
        return it->second;
    }
    // Return default config if not found
    return PumpConfig();
}

void ESPHomeHAL::setPumpConfig(const std::string& pumpId, float flowRateMLPerSec, float pwmIntensity) {
    pump_configs_[pumpId] = PumpConfig(pumpId, flowRateMLPerSec, pwmIntensity);
    ESP_LOGI(TAG, "Pump config set: %s - %.3f mL/s @ %.0f%% PWM",
             pumpId.c_str(), flowRateMLPerSec, pwmIntensity * 100.0f);
}

void ESPHomeHAL::setTankVolume(float volumeLiters) {
    tank_volume_liters_ = volumeLiters;
    ESP_LOGI(TAG, "Tank volume set: %.1f liters", volumeLiters);
}

float ESPHomeHAL::getTankVolume() const {
    return tank_volume_liters_;
}

void ESPHomeHAL::setMagValveFlowRate(float flowRateMLPerSec) {
    mag_valve_flow_rate_ml_s_ = flowRateMLPerSec;
    ESP_LOGI(TAG, "Magnetic valve flow rate set: %.1f mL/s (%.2f L/min)",
             flowRateMLPerSec, flowRateMLPerSec * 0.06f);
}

float ESPHomeHAL::getMagValveFlowRate() const {
    return mag_valve_flow_rate_ml_s_;
}

float ESPHomeHAL::valveflow(float targetML) {
    if (mag_valve_flow_rate_ml_s_ <= 0.0f) {
        ESP_LOGE(TAG, "valveflow: Invalid mag valve flow rate %.3f mL/s", mag_valve_flow_rate_ml_s_);
        return 0.0f;
    }

    float duration_s = targetML / mag_valve_flow_rate_ml_s_;
    ESP_LOGI(TAG, "valveflow(WaterValve): %.1f mL @ %.1f mL/s = %.2f seconds",
             targetML, mag_valve_flow_rate_ml_s_, duration_s);
    return duration_s;
}

void ESPHomeHAL::setValve(const std::string& valveId, bool state) {
    ESP_LOGI(TAG, "setValve(%s, %s)", valveId.c_str(), state ? "OPEN" : "CLOSED");

    // Update internal state tracking
    valve_states_[valveId] = state;

    // Route to appropriate GPIO output based on valve ID
    if (valveId == "WaterValve") {
        // Magnetic valve (fresh water inlet) on GPIO18
        if (mag_valve_output_) {
            if (state) {
                mag_valve_output_->turn_on();
            } else {
                mag_valve_output_->turn_off();
            }
        } else {
            ESP_LOGW(TAG, "Magnetic valve output not configured - cannot control WaterValve");
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

bool ESPHomeHAL::readWaterLevelHigh() {
    if (!water_level_high_sensor_) {
        return false;
    }
    return water_level_high_sensor_->state;
}

bool ESPHomeHAL::readWaterLevelLow() {
    if (!water_level_low_sensor_) {
        return false;
    }
    return water_level_low_sensor_->state;
}

bool ESPHomeHAL::hasWaterLevelSensors() const {
    return water_level_high_sensor_ != nullptr &&
           water_level_low_sensor_ != nullptr;
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
