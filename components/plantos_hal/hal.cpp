#include "hal.h"
#include "esphome/core/log.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/output/float_output.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/ezo_ph_uart/ezo_ph_uart.h"
#include "esp_http_client.h"
#include "esphome/core/time.h"
#include <cmath>
#include "esphome/components/actuator_safety_gate/ActuatorSafetyGate.h"
#include "esphome/components/tds_sensor/tds_sensor.h"

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

void ESPHomeHAL::set_temperature_sensor(esphome::sensor::Sensor* temperature_sensor) {
    temperature_sensor_ = temperature_sensor;
    ESP_LOGI(TAG, "Temperature sensor configured");
}

void ESPHomeHAL::set_ec_sensor(esphome::sensor::Sensor* ec_sensor) {
    ec_sensor_ = ec_sensor;
    ESP_LOGI(TAG, "EC sensor configured");
}

void ESPHomeHAL::set_tds_sensor_component(tds_sensor::TDSSensor* tds) {
    tds_sensor_component_ = tds;
    ESP_LOGI(TAG, "TDS sensor component configured for EC calibration");
}

void ESPHomeHAL::set_water_level_high_sensor(esphome::binary_sensor::BinarySensor* sensor) {
    water_level_high_sensor_ = sensor;
    ESP_LOGI(TAG, "Water level HIGH sensor configured");
}

void ESPHomeHAL::set_water_level_low_sensor(esphome::binary_sensor::BinarySensor* sensor) {
    water_level_low_sensor_ = sensor;
    ESP_LOGI(TAG, "Water level LOW sensor configured (GPIO23 - WTR_LO - auto-feed trigger level)");
}

void ESPHomeHAL::set_water_level_empty_sensor(esphome::binary_sensor::BinarySensor* sensor) {
    water_level_empty_sensor_ = sensor;
    ESP_LOGI(TAG, "Water level EMPTY sensor configured (GPIO22 - WTR_Empty - minimum safe level)");
}

void ESPHomeHAL::set_time_source(esphome::time::RealTimeClock* time_source) {
    time_source_ = time_source;
    ESP_LOGI(TAG, "Time source configured");
}

// ============================================================================
// ACTUATOR OUTPUT SETTERS (Phase 2: Hardware Control)
// ============================================================================

void ESPHomeHAL::set_mag_valve_output(esphome::output::FloatOutput* output) {
    mag_valve_output_ = output;
    ESP_LOGI(TAG, "Magnetic valve output configured (GPIO1 - MagValve - PWM capable)");
}

void ESPHomeHAL::set_pump_ph_output(esphome::output::FloatOutput* output) {
    pump_ph_output_ = output;
    ESP_LOGI(TAG, "pH pump output configured (GPIO4 - PP_1 - PWM capable)");
}

void ESPHomeHAL::set_pump_grow_output(esphome::output::FloatOutput* output) {
    pump_grow_output_ = output;
    ESP_LOGI(TAG, "Grow pump output configured (GPIO5 - PP_2 - PWM capable)");
}

void ESPHomeHAL::set_pump_micro_output(esphome::output::FloatOutput* output) {
    pump_micro_output_ = output;
    ESP_LOGI(TAG, "Micro pump output configured (GPIO6 - PP_3 - PWM capable)");
}

void ESPHomeHAL::set_pump_bloom_output(esphome::output::FloatOutput* output) {
    pump_bloom_output_ = output;
    ESP_LOGI(TAG, "Bloom pump output configured (GPIO7 - PP_4 - PWM capable)");
}

void ESPHomeHAL::set_pump_wastewater_output(esphome::output::FloatOutput* output) {
    pump_wastewater_output_ = output;
    ESP_LOGI(TAG, "Wastewater pump output configured (Shelly Socket 2 - HTTP control)");
}

void ESPHomeHAL::set_air_pump_switch(esphome::switch_::Switch* sw) {
    air_pump_switch_ = sw;
    ESP_LOGI(TAG, "Air pump switch configured (Shelly Socket 0 - HTTP control)");
}

void ESPHomeHAL::set_wastewater_pump_switch(esphome::switch_::Switch* sw) {
    wastewater_pump_switch_ = sw;
    ESP_LOGI(TAG, "Wastewater pump switch configured (Shelly Socket 2 - HTTP control)");
}

void ESPHomeHAL::set_grow_light_switch(esphome::switch_::Switch* sw) {
    grow_light_switch_ = sw;
    ESP_LOGI(TAG, "Grow light switch configured (Shelly Socket 3 - HTTP control)");
}

void ESPHomeHAL::set_actuator_safety_gate(esphome::actuator_safety_gate::ActuatorSafetyGate* asg) {
    actuator_safety_gate_ = asg;
    ESP_LOGI(TAG, "ActuatorSafetyGate configured for state sync");
}

// NOTE: set_pump_air_output removed - future Zigbee implementation

// ============================================================================
// COMPONENT LIFECYCLE
// ============================================================================

void ESPHomeHAL::setup() {
    ESP_LOGI(TAG, "PlantOS HAL initialized");

    // Start the Shelly worker task (all Shelly HTTP runs off the main loop)
    shelly_cmd_queue_ = xQueueCreate(8, sizeof(ShellyRequest));
    shelly_result_queue_ = xQueueCreate(8, sizeof(ShellyResult));
    if (shelly_cmd_queue_ && shelly_result_queue_ &&
        xTaskCreate(&ESPHomeHAL::shellyTaskEntry, "shelly", 6144, this, 1, &shelly_task_) == pdPASS) {
        ESP_LOGI(TAG, "Shelly worker started (%s, poll every %us)",
                 SHELLY_IP, SHELLY_POLL_INTERVAL_MS / 1000);
    } else {
        ESP_LOGE(TAG, "Failed to start Shelly worker - Shelly actuators unavailable");
    }

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
    if (!temperature_sensor_) {
        ESP_LOGW(TAG, "Temperature sensor not configured - temperature monitoring will be disabled");
    }
    if (!ec_sensor_) {
        ESP_LOGW(TAG, "EC sensor not configured - EC monitoring will be disabled");
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
    // Apply results from the Shelly worker (logging, ASG sync, switch publish)
    if (shelly_result_queue_) {
        ShellyResult result;
        while (xQueueReceive(shelly_result_queue_, &result, 0) == pdTRUE) {
            handleShellyResult(result);
        }
    }
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
        // pH pump (acid dosing) on GPIO4 (PP_1)
        if (pump_ph_output_) {
            pump_ph_output_->set_level(dutyCycle);
        } else {
            ESP_LOGW(TAG, "pH pump output not configured - cannot control AcidPump");
        }
    }
    else if (pumpId == "NutrientPumpA") {
        // Grow pump on GPIO5 (PP_2)
        if (pump_grow_output_) {
            pump_grow_output_->set_level(dutyCycle);
        } else {
            ESP_LOGW(TAG, "Grow pump output not configured - cannot control NutrientPumpA");
        }
    }
    else if (pumpId == "NutrientPumpB") {
        // Micro pump on GPIO6 (PP_3)
        if (pump_micro_output_) {
            pump_micro_output_->set_level(dutyCycle);
        } else {
            ESP_LOGW(TAG, "Micro pump output not configured - cannot control NutrientPumpB");
        }
    }
    else if (pumpId == "NutrientPumpC") {
        // Bloom pump on GPIO7 (PP_4)
        if (pump_bloom_output_) {
            pump_bloom_output_->set_level(dutyCycle);
        } else {
            ESP_LOGW(TAG, "Bloom pump output not configured - cannot control NutrientPumpC");
        }
    }
    else if (pumpId == "WastewaterPump") {
        // Wastewater pump via Shelly Socket 2 (HTTP direct control with retry)
        std::string url = std::string("http://") + SHELLY_IP + "/rpc/Switch.Set?id=2&on=" + (state ? "true" : "false");
        queueShellyRequest(url, "WastewaterPump");
        ESP_LOGD(TAG, "WastewaterPump → Shelly Socket 2 HTTP: %s", state ? "ON" : "OFF");
    }
    else if (pumpId == "AirPump") {
        // Air pump via Shelly Socket 0 - Use sequence API for consistency
        // This also stops any running sequence when turning on/off manually
        // NOTE: Debouncing is handled by ActuatorSafetyGate, not here
        std::string url = std::string("http://") + SHELLY_IP +
                          "/script/1/api?action=" + (state ? "on" : "off") + "&id=0";
        queueShellyRequest(url, "AirPump");
    }
    else if (pumpId == "GrowLight") {
        // Grow light via Shelly Socket 3 (HTTP direct control with retry)
        std::string url = std::string("http://") + SHELLY_IP + "/rpc/Switch.Set?id=3&on=" + (state ? "true" : "false");
        queueShellyRequest(url, "GrowLight");
        ESP_LOGI(TAG, "GrowLight → Shelly Socket 3 HTTP: %s", state ? "ON" : "OFF");
    }
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

void ESPHomeHAL::setPumpPwm(const std::string& pumpId, float pwmIntensity) {
    pwmIntensity = std::clamp(pwmIntensity, 0.0f, 1.0f);
    auto it = pump_configs_.find(pumpId);
    if (it != pump_configs_.end()) {
        it->second.pwm_intensity = pwmIntensity;   // preserve flow_rate_ml_s
    } else {
        pump_configs_[pumpId] = PumpConfig(pumpId, 1.0f, pwmIntensity);
    }
    ESP_LOGI(TAG, "setPumpPwm(%s) -> %.0f%%", pumpId.c_str(), pwmIntensity * 100.0f);
}

void ESPHomeHAL::setTankVolumeDelta(float volumeLiters) {
    tank_volume_delta_liters_ = volumeLiters;
    ESP_LOGI(TAG, "Tank volume delta set: %.1f liters (LOW→HIGH)", volumeLiters);
}

float ESPHomeHAL::getTankVolumeDelta() const {
    return tank_volume_delta_liters_;
}

void ESPHomeHAL::setTotalTankVolume(float volumeLiters) {
    total_tank_volume_liters_ = volumeLiters;
    ESP_LOGI(TAG, "Total tank volume set: %.1f liters (EMPTY→HIGH)", volumeLiters);
}

float ESPHomeHAL::getTotalTankVolume() const {
    return total_tank_volume_liters_;
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
        // Magnetic valve (fresh water inlet) on GPIO1 (MagValve)
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

bool ESPHomeHAL::checkShellySwitchStatus(const std::string& pumpId) {
    // TODO: Query Shelly via HTTP GET /rpc/Switch.GetStatus?id=X
    // ESPHome's callback-based HTTP makes synchronous requests difficult
    // For now, return tracked state
    return getPumpState(pumpId);
}

// ============================================================================
// SHELLY PATTERN API - AirPump Sequence Control
// ============================================================================

bool ESPHomeHAL::setAirPumpPattern(const std::vector<uint32_t>& pattern, bool finalState) {
    if (pattern.empty()) {
        ESP_LOGW(TAG, "setAirPumpPattern: empty pattern");
        return false;
    }

    // Build pattern string: "30,120,30"
    std::string patternStr;
    uint32_t totalDurationMs = 0;
    for (size_t i = 0; i < pattern.size(); i++) {
        if (i > 0) patternStr += ",";
        patternStr += std::to_string(pattern[i]);
        totalDurationMs += pattern[i] * 1000;  // Convert seconds to ms
    }

    // Build URL for Shelly script sequence API
    // API: http://192.168.0.130/script/1/api?action=sequence&id=0&pattern=X,Y,Z&finalstate=F
    std::string url = std::string("http://") + SHELLY_IP +
                      "/script/1/api?action=sequence&id=0&pattern=" +
                      patternStr + "&finalstate=" + (finalState ? "1" : "0");

    ESP_LOGI(TAG, "AirPump pattern: [%s], finalstate=%s, duration=%ums",
             patternStr.c_str(), finalState ? "ON" : "OFF", totalDurationMs);

    // Use multi-attempt to handle transient HTTP connection failures
    // Pattern commands are idempotent - Shelly script stops any existing sequence before starting new one
    if (!queueShellyRequest(url, "AirPump Pattern")) {
        return false;
    }

    // Track sequence state locally
    shelly_sequences_[0].running = true;
    shelly_sequences_[0].start_time = esphome::millis();
    shelly_sequences_[0].estimated_duration_ms = totalDurationMs;

    // Update tracked state to finalState (what it will be after pattern completes)
    pump_states_["AirPump"] = finalState;

    return true;
}

bool ESPHomeHAL::stopAirPumpSequence(bool finalState) {
    // NOTE: Debouncing is handled by ActuatorSafetyGate, not here
    // HAL is a dumb hardware layer that executes commands

    // Use sequence API to stop and set final state
    // action=on or action=off stops any running sequence and sets state
    std::string url = std::string("http://") + SHELLY_IP +
                      "/script/1/api?action=" + (finalState ? "on" : "off") + "&id=0";

    ESP_LOGI(TAG, "AirPump sequence stop → %s", finalState ? "ON" : "OFF");

    // Use multi-attempt to handle transient HTTP connection failures
    if (!queueShellyRequest(url, "AirPump Stop")) {
        return false;
    }

    // Clear sequence tracking
    shelly_sequences_[0].running = false;
    shelly_sequences_[0].estimated_duration_ms = 0;

    // Update tracked state
    pump_states_["AirPump"] = finalState;

    return true;
}

// ============================================================================
// SHELLY SEQUENCE AWARENESS - Check and stop running sequences
// ============================================================================

bool ESPHomeHAL::isSequenceRunning(uint8_t switchId) const {
    if (switchId > 3) {
        return false;
    }

    const auto& seq = shelly_sequences_[switchId];
    if (!seq.running) {
        return false;
    }

    // If we have an estimated duration, check if sequence should have completed
    if (seq.estimated_duration_ms > 0) {
        uint32_t elapsed = esphome::millis() - seq.start_time;
        if (elapsed >= seq.estimated_duration_ms) {
            // Sequence should have completed - don't mark as running
            // Note: Can't clear the flag here since this is a const method
            return false;
        }
    }

    return true;
}

void ESPHomeHAL::ensureNoSequenceRunning(uint8_t switchId, bool finalState) {
    if (switchId > 3) {
        ESP_LOGW(TAG, "ensureNoSequenceRunning: Invalid switch ID %d", switchId);
        return;
    }

    // Check if there's a running sequence we need to stop
    if (isSequenceRunning(switchId)) {
        ESP_LOGI(TAG, "Stopping running sequence on switch %d before new command", switchId);

        // Send stop command via script API (works for all switches 0-3)
        std::string url = std::string("http://") + SHELLY_IP +
                          "/script/1/api?action=" + (finalState ? "on" : "off") +
                          "&id=" + std::to_string(switchId);

        queueShellyRequest(url, "Sequence Stop");

        // Clear sequence tracking
        shelly_sequences_[switchId].running = false;
        shelly_sequences_[switchId].estimated_duration_ms = 0;
    }
}

void ESPHomeHAL::setShellySwitch(uint8_t switchId, bool state) {
    if (switchId > 3) {
        ESP_LOGW(TAG, "setShellySwitch: Invalid switch ID %d", switchId);
        return;
    }

    // Always use the script API for sequence-aware control
    // This ensures any running sequences are stopped properly
    // The Shelly script's on/off handlers call stopSequence() internally
    std::string url = std::string("http://") + SHELLY_IP +
                      "/script/1/api?action=" + (state ? "on" : "off") +
                      "&id=" + std::to_string(switchId);

    ESP_LOGI(TAG, "Shelly switch %d → %s (via script API)", switchId, state ? "ON" : "OFF");

    queueShellyRequest(url, "Shelly Switch");

    // Clear sequence tracking for this switch
    shelly_sequences_[switchId].running = false;
    shelly_sequences_[switchId].estimated_duration_ms = 0;

    // Update pump state tracking for known pumps
    if (switchId == 0) {
        pump_states_["AirPump"] = state;
    } else if (switchId == 2) {
        pump_states_["WastewaterPump"] = state;
    } else if (switchId == 3) {
        pump_states_["GrowLight"] = state;
    }
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

    // Call appropriate calibration method based on step (void methods — fire-and-forget)
    switch (calibrationStep) {
        case 0:  // Mid-point calibration
            ph_sensor_component_->calibrate_mid(calibrationPoint);
            return true;
        case 1:  // Low-point calibration
            ph_sensor_component_->calibrate_low(calibrationPoint);
            return true;
        case 2:  // High-point calibration
            ph_sensor_component_->calibrate_high(calibrationPoint);
            return true;
        default:
            ESP_LOGE(TAG, "Invalid calibration step: %d", calibrationStep);
            return false;
    }
}

bool ESPHomeHAL::isPhSensorReady() const {
    if (!ph_sensor_component_) return false;
    return ph_sensor_component_->is_sensor_ready();
}

void ESPHomeHAL::setPhVerbose(bool enable) {
    if (!ph_sensor_component_) return;
    ph_sensor_component_->set_verbose(enable);
}

void ESPHomeHAL::queryPhCalibrationStatus() {
    if (!ph_sensor_component_) return;
    ph_sensor_component_->query_calibration_status();
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

bool ESPHomeHAL::readWaterLevelEmpty() {
    if (!water_level_empty_sensor_) {
        ESP_LOGW(TAG, "EMPTY sensor not configured - returning false");
        return false;
    }
    return water_level_empty_sensor_->state;
}

bool ESPHomeHAL::hasWaterLevelSensors() const {
    return water_level_high_sensor_ != nullptr &&
           water_level_low_sensor_ != nullptr &&
           water_level_empty_sensor_ != nullptr;  // All 3 sensors required
}

float ESPHomeHAL::readLightIntensity() {
    // Light sensor removed from hardware - always return 0
    return 0.0f;
}

bool ESPHomeHAL::hasLightIntensity() const {
    // Light sensor removed from hardware
    return false;
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

float ESPHomeHAL::readEC() {
    if (!ec_sensor_ || !ec_sensor_->has_state()) {
        return 0.0f;
    }
    return ec_sensor_->state;
}

bool ESPHomeHAL::hasECValue() const {
    return ec_sensor_ && ec_sensor_->has_state();
}

void ESPHomeHAL::onECChange(std::function<void(float)> callback) {
    if (!ec_sensor_) {
        ESP_LOGW(TAG, "Cannot register EC callback - sensor not configured");
        return;
    }

    ec_sensor_->add_on_state_callback([callback](float value) {
        callback(value);
    });

    ESP_LOGD(TAG, "EC change callback registered");
}

bool ESPHomeHAL::setECCalibrationFactor(float factor) {
    if (!tds_sensor_component_) {
        ESP_LOGW(TAG, "Cannot set EC calibration factor - TDS sensor component not configured");
        return false;
    }
    tds_sensor_component_->set_calibration_factor(factor);
    return true;
}

float ESPHomeHAL::getECCalibrationFactor() const {
    if (!tds_sensor_component_) {
        return 1.0f;
    }
    return tds_sensor_component_->get_calibration_factor();
}

bool ESPHomeHAL::resetECCalibrationFactor() {
    if (!tds_sensor_component_) {
        ESP_LOGW(TAG, "Cannot reset EC calibration factor - TDS sensor component not configured");
        return false;
    }
    tds_sensor_component_->reset_calibration_factor();
    return true;
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

    // Throttle: every perform() is a full LightState update, so skip frames that
    // don't change the LED visibly and cap the rate. An on/off flip always goes through.
    bool on = brightness > 0.01f;
    uint32_t now = esphome::millis();
    if (on == led_is_on_) {
        bool changed = std::fabs(r - led_r_) > LED_MIN_DELTA ||
                       std::fabs(g - led_g_) > LED_MIN_DELTA ||
                       std::fabs(b - led_b_) > LED_MIN_DELTA ||
                       std::fabs(brightness - led_brightness_) > LED_MIN_DELTA;
        if (!changed || now - led_last_update_ms_ < LED_MIN_INTERVAL_MS) {
            return;
        }
    }

    // Use ESPHome's LightState API
    auto call = led_->make_call();
    call.set_state(on);
    call.set_brightness(brightness);
    call.set_rgb(r, g, b);
    call.perform();

    led_r_ = r;
    led_g_ = g;
    led_b_ = b;
    led_brightness_ = brightness;
    led_last_update_ms_ = now;
    led_is_on_ = on;
}

void ESPHomeHAL::turnOffLED() {
    if (!led_ || !led_is_on_) {
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

int64_t ESPHomeHAL::getCurrentTimestamp() const {
    if (!time_source_ || !time_source_->now().is_valid()) {
        return 0;  // Time not available
    }
    return time_source_->now().timestamp;
}

uint32_t ESPHomeHAL::getSecondsSinceMidnight() const {
    if (!time_source_ || !time_source_->now().is_valid()) {
        return 0;  // Time not available
    }

    auto now = time_source_->now();
    // Calculate seconds since midnight: hour * 3600 + minute * 60 + second
    uint32_t seconds = now.hour * 3600 + now.minute * 60 + now.second;
    return seconds;
}

bool ESPHomeHAL::hasTime() const {
    return time_source_ != nullptr && time_source_->now().is_valid();
}

// ============================================================================
// SHELLY WORKER - async HTTP in a FreeRTOS task
// ============================================================================

namespace {

// Blocking GET, called from the Shelly task only. Returns the HTTP status or -1.
// Reads up to body_size-1 bytes of the response into body (NUL-terminated).
int shellyHttpGet(const char* url, uint32_t timeout_ms, char* body, size_t body_size) {
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = static_cast<int>(timeout_ms);
    cfg.disable_auto_redirect = true;
    cfg.keep_alive_enable = false;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return -1;
    }
    esp_http_client_set_header(client, "Connection", "close");

    int status = -1;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (body && body_size > 0) {
            int n = esp_http_client_read_response(client, body, static_cast<int>(body_size) - 1);
            body[n > 0 ? n : 0] = '\0';
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return status;
}

// Parse {"status":"ok","uptime":X,"switches":{"0":true,"2":false,"3":true}}
void parseShellyStates(const char* body, uint32_t& uptime, int8_t states[4]) {
    const char* uptime_key = strstr(body, "\"uptime\":");
    if (uptime_key) {
        uptime_key += 9;
        while (*uptime_key == ' ' || *uptime_key == '\t') uptime_key++;
        uptime = 0;
        while (*uptime_key >= '0' && *uptime_key <= '9') {
            uptime = uptime * 10 + (*uptime_key - '0');
            uptime_key++;
        }
    }
    for (int i = 0; i < 4; i++) {
        char search[8];
        snprintf(search, sizeof(search), "\"%d\":", i);
        const char* pos = strstr(body, search);
        states[i] = -1;
        if (!pos) continue;
        pos += strlen(search);
        while (*pos == ' ' || *pos == '\t') pos++;
        if (*pos == 't') states[i] = 1;
        else if (*pos == 'f') states[i] = 0;
    }
}

}  // namespace

void ESPHomeHAL::shellyTaskEntry(void* arg) {
    static_cast<ESPHomeHAL*>(arg)->shellyTaskLoop();
}

void ESPHomeHAL::shellyTaskLoop() {
    // esp_http_client logs through ESPHome's logger hook, which must only run on the
    // main loop. Silence it here; failures are reported via ShellyResult instead.
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_NONE);
    esp_log_level_set("esp-tls", ESP_LOG_NONE);
    esp_log_level_set("transport_base", ESP_LOG_NONE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_NONE);
    esp_log_level_set("transport", ESP_LOG_NONE);

    uint32_t next_poll = 0;
    for (;;) {
        // Commands first. Wait at most 1 s so enable/poll requests are picked up quickly.
        ShellyRequest req;
        if (xQueueReceive(shelly_cmd_queue_, &req, pdMS_TO_TICKS(1000)) == pdTRUE) {
            ShellyResult res = {};
            strncpy(res.name, req.name, sizeof(res.name) - 1);
            for (uint8_t attempt = 0; attempt < req.attempts; attempt++) {
                if (attempt > 0) vTaskDelay(pdMS_TO_TICKS(SHELLY_RETRY_DELAY_MS));
                res.status = shellyHttpGet(req.url, SHELLY_HTTP_TIMEOUT_MS, nullptr, 0);
                res.ok = res.status >= 200 && res.status < 300;
                if (res.ok) break;
            }
            xQueueSend(shelly_result_queue_, &res, 0);
            continue;
        }

        uint32_t now = esphome::millis();
        bool poll_due = shelly_poll_now_.exchange(false) || (int32_t)(now - next_poll) >= 0;
        if (!shelly_enabled_ || !poll_due) {
            continue;
        }
        next_poll = now + SHELLY_POLL_INTERVAL_MS;

        char url[64];
        snprintf(url, sizeof(url), "http://%s/script/1/api?action=states", SHELLY_IP);
        char body[512];
        body[0] = '\0';
        ShellyResult res = {};
        strncpy(res.name, "Poll", sizeof(res.name) - 1);
        res.is_poll = true;
        res.status = shellyHttpGet(url, SHELLY_HTTP_TIMEOUT_MS, body, sizeof(body));
        res.ok = res.status == 200;
        for (auto& st : res.states) st = -1;
        if (res.ok) {
            parseShellyStates(body, res.uptime, res.states);
        }
        xQueueSend(shelly_result_queue_, &res, 0);
    }
}

bool ESPHomeHAL::queueShellyRequest(const std::string& url, const char* name, uint8_t attempts,
                                    bool force) {
    if (!shelly_enabled_ && !force) {
        if (!shelly_disabled_warned_) {
            ESP_LOGW(TAG, "Shelly integration disabled - dropping %s command (further drops not logged)",
                     name);
            shelly_disabled_warned_ = true;
        }
        return false;
    }
    if (!shelly_cmd_queue_) {
        ESP_LOGE(TAG, "%s: Shelly worker not running", name);
        return false;
    }

    ShellyRequest req = {};
    if (url.size() >= sizeof(req.url)) {
        ESP_LOGE(TAG, "%s: URL too long (%u bytes)", name, (unsigned) url.size());
        return false;
    }
    strncpy(req.url, url.c_str(), sizeof(req.url) - 1);
    strncpy(req.name, name, sizeof(req.name) - 1);
    req.attempts = attempts;

    if (xQueueSend(shelly_cmd_queue_, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "%s: Shelly command queue full - command dropped", name);
        return false;
    }
    ESP_LOGD(TAG, "%s: queued %s", name, req.url);
    return true;
}

void ESPHomeHAL::handleShellyResult(const ShellyResult& result) {
    if (!result.is_poll) {
        if (result.ok) {
            ESP_LOGD(TAG, "%s: OK (status %d)", result.name, result.status);
        } else {
            ESP_LOGW(TAG, "%s: failed (status %d)", result.name, result.status);
        }
        return;
    }

    if (!shelly_enabled_) {
        return;  // Poll finished after the integration was disabled - ignore
    }

    if (!result.ok) {
        // Log only the first failure of a streak to avoid log spam while the Shelly is offline
        if (shelly_poll_failures_ == 0) {
            ESP_LOGW(TAG, "Shelly poll failed (status %d) - Shelly offline?", result.status);
        }
        if (shelly_poll_failures_ < 255) shelly_poll_failures_++;
        updateShellyHealth(false, 0);
        return;
    }

    if (shelly_poll_failures_ > 0) {
        ESP_LOGI(TAG, "Shelly reachable again after %u failed polls", shelly_poll_failures_);
        shelly_poll_failures_ = 0;
    }
    updateShellyHealth(true, result.uptime);

    // Sync HAL state, ASG actual state and web UI toggles: 0=AirPump, 2=WastewaterPump, 3=GrowLight
    struct { uint8_t id; const char* actuator; esphome::switch_::Switch* sw; } map[] = {
        {0, "AirPump", air_pump_switch_},
        {2, "WastewaterPump", wastewater_pump_switch_},
        {3, "GrowLight", grow_light_switch_},
    };
    for (const auto& m : map) {
        if (result.states[m.id] < 0) continue;
        bool on = result.states[m.id] == 1;
        updateShellySwitchState(m.id, on);
        if (actuator_safety_gate_) {
            actuator_safety_gate_->updateActualState(m.actuator, on);
        }
        if (m.sw) {
            m.sw->publish_state(on);
        }
    }
    ESP_LOGD(TAG, "Shelly states: AirPump=%d, Wastewater=%d, GrowLight=%d, uptime=%us",
             result.states[0], result.states[2], result.states[3], result.uptime);

    // AirPump should run continuously in Normal mode - turn it back on if the Shelly reports OFF
    if (result.states[0] == 0 && actuator_safety_gate_ &&
        !actuator_safety_gate_->isCyclingEnabled("AirPump")) {
        ESP_LOGI(TAG, "AirPump OFF but Normal mode desired - activating via ASG");
        actuator_safety_gate_->enableCycling("AirPump", false);
    }
}

void ESPHomeHAL::setShellyEnabled(bool enabled) {
    if (enabled == shelly_enabled_) {
        return;
    }
    if (enabled) {
        shelly_disabled_warned_ = false;
        shelly_poll_failures_ = 0;
        shelly_enabled_ = true;
        shelly_poll_now_ = true;  // Re-sync states right away
        ESP_LOGI(TAG, "Shelly integration ENABLED");
    } else {
        // Never leave the drain pump running unmanaged: queue OFF before disabling
        // (no queue yet = called during boot restore, before the worker started)
        if (shelly_cmd_queue_) {
            std::string url = std::string("http://") + SHELLY_IP + "/rpc/Switch.Set?id=2&on=false";
            queueShellyRequest(url, "WastewaterPump", 2, true);
        }
        pump_states_["WastewaterPump"] = false;
        shelly_enabled_ = false;
        shelly_reachable_ = false;
        ESP_LOGW(TAG, "Shelly integration DISABLED - no polling, commands dropped "
                      "(AirPump, WastewaterPump, GrowLight unmanaged)");
    }
}

bool ESPHomeHAL::isShellyReachable() const {
    // Consider offline if no successful ping in last 60 seconds
    if (!shelly_enabled_ || !shelly_reachable_) return false;
    uint32_t age = esphome::millis() - shelly_last_ping_ms_;
    return age < 60000;  // 60 second timeout
}

uint32_t ESPHomeHAL::getShellyUptime() const {
    return shelly_uptime_seconds_;
}

void ESPHomeHAL::updateShellyHealth(bool reachable, uint32_t uptime) {
    // Update health status (can be called from YAML callbacks or internally)
    bool changed = reachable != shelly_reachable_;
    shelly_reachable_ = reachable;
    if (reachable) {
        shelly_uptime_seconds_ = uptime;
        shelly_last_ping_ms_ = esphome::millis();
    }
    if (changed) {
        ESP_LOGI(TAG, "Shelly health: %s (uptime: %us)", reachable ? "ONLINE" : "OFFLINE", uptime);
    }
}

// ============================================================================
// SHELLY STATE SYNCHRONIZATION - For debouncing and UI state
// ============================================================================

void ESPHomeHAL::updateShellySwitchState(uint8_t switchId, bool state) {
    if (switchId > 3) {
        ESP_LOGW(TAG, "updateShellySwitchState: Invalid switch ID %d", switchId);
        return;
    }

    bool previousState = shelly_switch_states_[switchId];
    shelly_switch_states_[switchId] = state;
    shelly_state_update_time_ = esphome::millis();

    // Also update pump_states_ map for consistency with getPumpState()
    if (switchId == 0) {
        pump_states_["AirPump"] = state;
    } else if (switchId == 2) {
        pump_states_["WastewaterPump"] = state;
    } else if (switchId == 3) {
        pump_states_["GrowLight"] = state;
    }

    if (previousState != state) {
        ESP_LOGI(TAG, "Shelly switch %d state updated: %s → %s (from polling)",
                 switchId, previousState ? "ON" : "OFF", state ? "ON" : "OFF");
    }
}

bool ESPHomeHAL::getShellySwitchState(uint8_t switchId) const {
    if (switchId > 3) {
        return false;
    }
    return shelly_switch_states_[switchId];
}

uint32_t ESPHomeHAL::getLastShellyStateUpdate() const {
    return shelly_state_update_time_;
}

} // namespace plantos_hal
