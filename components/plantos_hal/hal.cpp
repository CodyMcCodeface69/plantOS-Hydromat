#include "hal.h"
#include "esphome/core/log.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/output/float_output.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/ezo_ph_uart/ezo_ph_uart.h"
#include "esphome/components/http_request/http_request.h"
#include "esphome/core/time.h"

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
    ESP_LOGI(TAG, "Water level LOW sensor configured (GPIO11 - auto-feed trigger level)");
}

void ESPHomeHAL::set_water_level_empty_sensor(esphome::binary_sensor::BinarySensor* sensor) {
    water_level_empty_sensor_ = sensor;
    ESP_LOGI(TAG, "Water level EMPTY sensor configured (GPIO16 - minimum safe level)");
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

void ESPHomeHAL::set_air_pump_switch(esphome::switch_::Switch* sw) {
    air_pump_switch_ = sw;
    ESP_LOGI(TAG, "Air pump switch configured (Shelly Socket 0 - HTTP control)");
}

void ESPHomeHAL::set_wastewater_pump_switch(esphome::switch_::Switch* sw) {
    wastewater_pump_switch_ = sw;
    ESP_LOGI(TAG, "Wastewater pump switch configured (Shelly Socket 2 - HTTP control)");
}

void ESPHomeHAL::set_http_request(esphome::http_request::HttpRequestComponent* http) {
    http_request_ = http;
    ESP_LOGI(TAG, "HTTP request component configured for Shelly control");
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
    uint32_t now = esphome::millis();

    // Check for HTTP request timeout and clear the in-progress flag
    // This prevents socket exhaustion by ensuring stale requests don't block new ones
    if (http_request_in_progress_) {
        if (now - http_request_start_time_ >= HTTP_REQUEST_TIMEOUT) {
            ESP_LOGW(TAG, "HTTP request timed out after %ums - clearing in-progress flag",
                     now - http_request_start_time_);
            http_request_in_progress_ = false;
        }
    }

    // Process scheduled Shelly HTTP retry attempts
    // This handles transient connection failures by sending commands multiple times
    if (shelly_retry_attempts_ > 0) {
        if (now >= shelly_retry_next_time_) {
            // Check if we can send (no other request in progress)
            if (!canSendHttpRequest()) {
                // Reschedule for later
                shelly_retry_next_time_ = now + 500;
                ESP_LOGD(TAG, "%s: Retry delayed - HTTP request in progress",
                         shelly_retry_device_name_.c_str());
            } else {
                shelly_retry_attempts_--;
                ESP_LOGD(TAG, "%s: Retry attempt (%d remaining)",
                         shelly_retry_device_name_.c_str(), shelly_retry_attempts_);

                // Mark request as started
                markHttpRequestStarted();

                // Send retry - use retry URL (may be different from url_cache_ if another command was sent)
                url_cache_ = shelly_retry_url_;
                http_request_->get(url_cache_);

                if (shelly_retry_attempts_ > 0) {
                    shelly_retry_next_time_ = now + 500;  // Schedule next attempt in 500ms
                }
            }
        }
    }

    // Check for Shelly ping timeout
    // If we sent a ping and haven't received a response within SHELLY_PING_TIMEOUT, mark as offline
    if (shelly_ping_pending_) {
        if (now - shelly_ping_sent_ms_ >= SHELLY_PING_TIMEOUT) {
            ESP_LOGW(TAG, "Shelly ping timeout - marking as OFFLINE");
            shelly_ping_pending_ = false;

            // Mark HTTP request as completed (timed out)
            markHttpRequestCompleted();

            // Mark as offline
            shelly_reachable_ = false;

            // Call stored callback with offline status
            if (shelly_ping_callback_) {
                shelly_ping_callback_(false, 0);
                shelly_ping_callback_ = nullptr;
            }
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
        // Wastewater pump via Shelly Socket 2 (HTTP direct control with retry)
        std::string url = std::string("http://") + SHELLY_IP + "/rpc/Switch.Set?id=2&on=" + (state ? "true" : "false");
        sendShellyCommand(url, "WastewaterPump (Socket 2)");
        ESP_LOGD(TAG, "WastewaterPump → Shelly Socket 2 HTTP: %s", state ? "ON" : "OFF");
    }
    else if (pumpId == "AirPump") {
        // Air pump via Shelly Socket 0 - Use sequence API for consistency
        // This also stops any running sequence when turning on/off manually
        // Uses multi-attempt to handle transient HTTP connection failures
        if (http_request_) {
            std::string url = std::string("http://") + SHELLY_IP +
                              "/script/1/api?action=" + (state ? "on" : "off") + "&id=0";
            sendShellyMultiAttempt(url, "AirPump", 1);  // Single attempt (retries disabled for now)
        }
    }
    else if (pumpId == "GrowLight") {
        // Grow light via Shelly Socket 3 (HTTP direct control with retry)
        std::string url = std::string("http://") + SHELLY_IP + "/rpc/Switch.Set?id=3&on=" + (state ? "true" : "false");
        sendShellyCommand(url, "GrowLight (Socket 3)");
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

bool ESPHomeHAL::sendShellyCommand(const std::string& url, const char* deviceName, int maxRetries) {
    // Robust HTTP request with retry logic
    // NOTE: This function uses blocking delays which is acceptable for infrequent actuator commands.
    // Strategy: Try sending the command up to maxRetries times with 2s delay between attempts.
    // This is safe because Shelly Switch.Set commands are idempotent (setting ON multiple times is harmless).

    if (!http_request_) {
        ESP_LOGE(TAG, "%s: HTTP request component not configured!", deviceName);
        return false;
    }

    // Check if we can send (no other request in progress)
    if (!canSendHttpRequest()) {
        ESP_LOGW(TAG, "%s: HTTP request blocked - another request in progress", deviceName);
        return false;
    }

    // Mark request as started
    markHttpRequestStarted();

    // Send the command asynchronously (fire-and-forget)
    // The ESPHome http_request component handles retries internally
    ESP_LOGI(TAG, "%s: Sending HTTP command", deviceName);
    ESP_LOGD(TAG, "%s: URL: %s", deviceName, url.c_str());

    // CRITICAL FIX: Cache URL to prevent use-after-free
    // The http_request component uses the URL asynchronously, so we must keep it alive
    // by storing it in a member variable that persists beyond this function call
    url_cache_ = url;

    // Send HTTP request (asynchronous - returns immediately)
    // Pass cached URL to prevent memory access fault when the local 'url' is destroyed
    http_request_->get(url_cache_);

    ESP_LOGI(TAG, "%s: HTTP command sent (async)", deviceName);
    return true;
}

void ESPHomeHAL::sendShellyMultiAttempt(const std::string& url, const char* deviceName, uint8_t attempts) {
    // Send HTTP command multiple times with delay between attempts
    // Works around transient connection failures without complex error detection
    // Safe because Shelly commands are idempotent (sending ON twice is harmless)
    //
    // This works for ALL Shelly command types:
    // - Simple ON/OFF: /script/1/api?action=on&id=0
    // - Pattern/Sequence: /script/1/api?action=sequence&id=0&pattern=30,120,30&finalstate=1
    // - Stop: /script/1/api?action=stop&id=0

    if (!http_request_ || attempts == 0) {
        ESP_LOGW(TAG, "%s: Cannot send - HTTP not configured or 0 attempts", deviceName);
        return;
    }

    // Check if we can send (no other request in progress)
    if (!canSendHttpRequest()) {
        ESP_LOGW(TAG, "%s: HTTP request blocked - another request in progress", deviceName);
        // Store for later retry via loop() scheduler
        shelly_retry_url_ = url;
        shelly_retry_device_name_ = deviceName;
        shelly_retry_attempts_ = attempts;
        shelly_retry_next_time_ = esphome::millis() + 1000;  // Retry in 1 second
        return;
    }

    // Mark request as started
    markHttpRequestStarted();

    ESP_LOGI(TAG, "%s: Sending HTTP command (%d attempts): %s", deviceName, attempts, url.c_str());

    // First attempt immediately - cache URL to prevent use-after-free
    url_cache_ = url;
    http_request_->get(url_cache_);

    // Schedule additional attempts if needed (retries disabled by default, attempts=1)
    if (attempts > 1) {
        // Store URL and device name for retry (must persist beyond this function)
        shelly_retry_url_ = url;
        shelly_retry_device_name_ = deviceName;
        shelly_retry_attempts_ = attempts - 1;
        shelly_retry_next_time_ = esphome::millis() + 500;  // 500ms delay between attempts
    }
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
    if (!http_request_ || pattern.empty()) {
        ESP_LOGW(TAG, "setAirPumpPattern: HTTP not configured or empty pattern");
        return false;
    }

    // Build pattern string: "30,120,30"
    std::string patternStr;
    for (size_t i = 0; i < pattern.size(); i++) {
        if (i > 0) patternStr += ",";
        patternStr += std::to_string(pattern[i]);
    }

    // Build URL for Shelly script sequence API
    // API: http://192.168.0.130/script/1/api?action=sequence&id=0&pattern=X,Y,Z&finalstate=F
    std::string url = std::string("http://") + SHELLY_IP +
                      "/script/1/api?action=sequence&id=0&pattern=" +
                      patternStr + "&finalstate=" + (finalState ? "1" : "0");

    ESP_LOGI(TAG, "AirPump pattern: [%s], finalstate=%s", patternStr.c_str(), finalState ? "ON" : "OFF");

    // Use multi-attempt to handle transient HTTP connection failures
    // Pattern commands are idempotent - Shelly script stops any existing sequence before starting new one
    sendShellyMultiAttempt(url, "AirPump Pattern", 1);  // Single attempt (retries disabled for now)

    // Update tracked state to finalState (what it will be after pattern completes)
    pump_states_["AirPump"] = finalState;

    return true;
}

bool ESPHomeHAL::stopAirPumpSequence(bool finalState) {
    if (!http_request_) {
        ESP_LOGW(TAG, "stopAirPumpSequence: HTTP not configured");
        return false;
    }

    // Use sequence API to stop and set final state
    // action=on or action=off stops any running sequence and sets state
    std::string url = std::string("http://") + SHELLY_IP +
                      "/script/1/api?action=" + (finalState ? "on" : "off") + "&id=0";

    ESP_LOGI(TAG, "AirPump sequence stop → %s", finalState ? "ON" : "OFF");

    // Use multi-attempt to handle transient HTTP connection failures
    sendShellyMultiAttempt(url, "AirPump Stop", 1);  // Single attempt (retries disabled for now)

    // Update tracked state
    pump_states_["AirPump"] = finalState;

    return true;
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
// SHELLY HEALTH CHECK - Device monitoring via HTTP ping
// ============================================================================

void ESPHomeHAL::pingShellyDevice(std::function<void(bool, uint32_t)> callback) {
    if (!http_request_) {
        ESP_LOGW(TAG, "HTTP request component not configured - cannot ping Shelly");
        if (callback) callback(false, 0);
        return;
    }

    // If a ping is already pending, don't send another one
    if (shelly_ping_pending_) {
        ESP_LOGD(TAG, "Shelly ping already pending - skipping");
        return;
    }

    // Store callback for when response arrives (or timeout)
    shelly_ping_callback_ = callback;

    // Send ping request to Shelly script API
    std::string url = std::string("http://") + SHELLY_IP + "/script/1/api?action=ping";
    ESP_LOGD(TAG, "Pinging Shelly at %s", url.c_str());

    // Cache URL to prevent use-after-free
    url_cache_ = url;
    http_request_->get(url_cache_);

    // Mark ping as pending and record send time for timeout detection
    shelly_ping_pending_ = true;
    shelly_ping_sent_ms_ = esphome::millis();
}

bool ESPHomeHAL::isShellyReachable() const {
    // Consider offline if no successful ping in last 60 seconds
    if (!shelly_reachable_) return false;
    uint32_t age = esphome::millis() - shelly_last_ping_ms_;
    return age < 60000;  // 60 second timeout
}

uint32_t ESPHomeHAL::getShellyUptime() const {
    return shelly_uptime_seconds_;
}

void ESPHomeHAL::updateShellyHealth(bool reachable, uint32_t uptime) {
    // Clear pending flag - we got a response (or explicit update)
    shelly_ping_pending_ = false;

    // Mark HTTP request as completed (response received)
    markHttpRequestCompleted();

    shelly_reachable_ = reachable;
    if (reachable) {
        shelly_uptime_seconds_ = uptime;
        shelly_last_ping_ms_ = esphome::millis();
    }
    ESP_LOGI(TAG, "Shelly health updated: %s (uptime: %us)",
             reachable ? "ONLINE" : "OFFLINE", uptime);

    // Call stored callback if any
    if (shelly_ping_callback_) {
        shelly_ping_callback_(reachable, uptime);
        shelly_ping_callback_ = nullptr;  // Clear after use
    }
}

// ============================================================================
// HTTP REQUEST SERIALIZATION - Prevent socket exhaustion
// ============================================================================

bool ESPHomeHAL::canSendHttpRequest() {
    uint32_t now = esphome::millis();

    // If no request in progress, OK to send
    if (!http_request_in_progress_) {
        return true;
    }

    // If request has timed out, clear the flag and allow new request
    if (now - http_request_start_time_ >= HTTP_REQUEST_TIMEOUT) {
        ESP_LOGW(TAG, "Previous HTTP request timed out after %ums - clearing flag",
                 now - http_request_start_time_);
        http_request_in_progress_ = false;
        return true;
    }

    // Request still in progress
    ESP_LOGD(TAG, "HTTP request in progress for %ums - waiting",
             now - http_request_start_time_);
    return false;
}

void ESPHomeHAL::markHttpRequestStarted() {
    http_request_in_progress_ = true;
    http_request_start_time_ = esphome::millis();
    ESP_LOGD(TAG, "HTTP request started");
}

void ESPHomeHAL::markHttpRequestCompleted() {
    if (http_request_in_progress_) {
        uint32_t duration = esphome::millis() - http_request_start_time_;
        ESP_LOGD(TAG, "HTTP request completed in %ums", duration);
    }
    http_request_in_progress_ = false;
}

} // namespace plantos_hal
