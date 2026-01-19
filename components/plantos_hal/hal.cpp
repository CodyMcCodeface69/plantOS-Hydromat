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

// Connection: close header to ensure ESP32 closes socket immediately after response
// Prevents socket exhaustion from lingering connections
static const std::list<esphome::http_request::Header> CONNECTION_CLOSE_HEADERS = {
    {"Connection", "close"}
};

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
    // This is a safety net - with synchronous requests this should rarely trigger
    if (http_request_in_progress_) {
        if (now - http_request_start_time_ >= HTTP_REQUEST_TIMEOUT) {
            ESP_LOGW(TAG, "HTTP request timed out after %ums - clearing in-progress flag",
                     now - http_request_start_time_);
            http_request_in_progress_ = false;
        }
    }

    // Process scheduled Shelly HTTP retry attempts
    // This handles transient connection failures by retrying commands
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

                // Send retry with proper response handling
                url_cache_ = shelly_retry_url_;
                auto container = http_request_->get(url_cache_, CONNECTION_CLOSE_HEADERS);

                if (container) {
                    if (container->status_code >= 200 && container->status_code < 300) {
                        ESP_LOGD(TAG, "%s: Retry success (status %d)",
                                 shelly_retry_device_name_.c_str(), container->status_code);
                        shelly_retry_attempts_ = 0;  // Success - no more retries needed
                    } else {
                        ESP_LOGW(TAG, "%s: Retry failed (status %d)",
                                 shelly_retry_device_name_.c_str(), container->status_code);
                    }
                    container->end();
                }

                markHttpRequestCompleted();

                if (shelly_retry_attempts_ > 0) {
                    shelly_retry_next_time_ = now + 500;  // Schedule next attempt in 500ms
                }
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
        // NOTE: Debouncing is handled by ActuatorSafetyGate, not here
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
    // Robust HTTP request with proper response handling
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

    ESP_LOGI(TAG, "%s: Sending HTTP command", deviceName);
    ESP_LOGD(TAG, "%s: URL: %s", deviceName, url.c_str());

    // Cache URL to prevent use-after-free
    url_cache_ = url;

    // Send HTTP request and properly handle the response
    // CRITICAL: We must consume the response to properly close the connection
    auto container = http_request_->get(url_cache_, CONNECTION_CLOSE_HEADERS);

    bool success = false;
    if (container) {
        if (container->status_code >= 200 && container->status_code < 300) {
            ESP_LOGI(TAG, "%s: HTTP success (status %d, %ums)",
                     deviceName, container->status_code, container->duration_ms);
            success = true;
        } else if (container->status_code > 0) {
            ESP_LOGW(TAG, "%s: HTTP error status %d", deviceName, container->status_code);
        } else {
            ESP_LOGW(TAG, "%s: HTTP request failed (no response)", deviceName);
        }

        // CRITICAL: Call end() to properly close the connection and free resources
        container->end();
    } else {
        ESP_LOGW(TAG, "%s: HTTP request returned null container", deviceName);
    }

    // Mark request as completed immediately (synchronous request)
    markHttpRequestCompleted();

    return success;
}

void ESPHomeHAL::sendShellyMultiAttempt(const std::string& url, const char* deviceName, uint8_t attempts) {
    // Send HTTP command with proper response handling
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

    ESP_LOGI(TAG, "%s: Sending HTTP command: %s", deviceName, url.c_str());

    // Cache URL to prevent use-after-free
    url_cache_ = url;

    // Send HTTP request and properly handle the response
    // CRITICAL: We must consume the response to properly close the connection
    auto container = http_request_->get(url_cache_, CONNECTION_CLOSE_HEADERS);

    if (container) {
        // Check response status
        if (container->status_code >= 200 && container->status_code < 300) {
            ESP_LOGD(TAG, "%s: HTTP success (status %d, %ums)",
                     deviceName, container->status_code, container->duration_ms);
        } else if (container->status_code > 0) {
            ESP_LOGW(TAG, "%s: HTTP error status %d", deviceName, container->status_code);
        } else {
            ESP_LOGW(TAG, "%s: HTTP request failed (no response)", deviceName);
        }

        // CRITICAL: Call end() to properly close the connection and free resources
        container->end();
    } else {
        ESP_LOGW(TAG, "%s: HTTP request returned null container", deviceName);
    }

    // Mark request as completed immediately (synchronous request)
    markHttpRequestCompleted();
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
    sendShellyMultiAttempt(url, "AirPump Pattern", 1);  // Single attempt (retries disabled for now)

    // Track sequence state locally
    shelly_sequences_[0].running = true;
    shelly_sequences_[0].start_time = esphome::millis();
    shelly_sequences_[0].estimated_duration_ms = totalDurationMs;

    // Update tracked state to finalState (what it will be after pattern completes)
    pump_states_["AirPump"] = finalState;

    return true;
}

bool ESPHomeHAL::stopAirPumpSequence(bool finalState) {
    if (!http_request_) {
        ESP_LOGW(TAG, "stopAirPumpSequence: HTTP not configured");
        return false;
    }

    // NOTE: Debouncing is handled by ActuatorSafetyGate, not here
    // HAL is a dumb hardware layer that executes commands

    // Use sequence API to stop and set final state
    // action=on or action=off stops any running sequence and sets state
    std::string url = std::string("http://") + SHELLY_IP +
                      "/script/1/api?action=" + (finalState ? "on" : "off") + "&id=0";

    ESP_LOGI(TAG, "AirPump sequence stop → %s", finalState ? "ON" : "OFF");

    // Use multi-attempt to handle transient HTTP connection failures
    sendShellyMultiAttempt(url, "AirPump Stop", 1);  // Single attempt (retries disabled for now)

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

        sendShellyMultiAttempt(url, "Sequence Stop", 1);

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

    if (!http_request_) {
        ESP_LOGW(TAG, "setShellySwitch: HTTP not configured");
        return;
    }

    // Always use the script API for sequence-aware control
    // This ensures any running sequences are stopped properly
    // The Shelly script's on/off handlers call stopSequence() internally
    std::string url = std::string("http://") + SHELLY_IP +
                      "/script/1/api?action=" + (state ? "on" : "off") +
                      "&id=" + std::to_string(switchId);

    ESP_LOGI(TAG, "Shelly switch %d → %s (via script API)", switchId, state ? "ON" : "OFF");

    sendShellyMultiAttempt(url, "Shelly Switch", 1);

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

    // Check if we can send (no other request in progress)
    if (!canSendHttpRequest()) {
        ESP_LOGD(TAG, "Shelly ping skipped - HTTP request in progress");
        return;
    }

    // Mark request as started
    markHttpRequestStarted();

    // Send ping request to Shelly script API
    std::string url = std::string("http://") + SHELLY_IP + "/script/1/api?action=ping";
    ESP_LOGD(TAG, "Pinging Shelly at %s", url.c_str());

    // Cache URL to prevent use-after-free
    url_cache_ = url;

    // Send HTTP request and properly handle the response
    auto container = http_request_->get(url_cache_, CONNECTION_CLOSE_HEADERS);

    bool reachable = false;
    uint32_t uptime = 0;

    if (container) {
        if (container->status_code == 200) {
            // Try to parse uptime from response body
            // Response format: {"status": "ok", "uptime": <seconds>, "ts": <timestamp_ms>}
            // We'll just check status code for now - YAML callback parses the JSON
            reachable = true;
            ESP_LOGD(TAG, "Shelly ping success (status %d, %ums)",
                     container->status_code, container->duration_ms);
        } else if (container->status_code > 0) {
            ESP_LOGW(TAG, "Shelly ping failed (status %d)", container->status_code);
        } else {
            ESP_LOGW(TAG, "Shelly ping failed (no response)");
        }

        // CRITICAL: Call end() to properly close the connection and free resources
        container->end();
    } else {
        ESP_LOGW(TAG, "Shelly ping returned null container");
    }

    // Mark request as completed
    markHttpRequestCompleted();

    // Update health status
    shelly_reachable_ = reachable;
    if (reachable) {
        shelly_last_ping_ms_ = esphome::millis();
    }

    // Call callback if provided
    if (callback) {
        callback(reachable, uptime);
    }
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
    // Update health status (can be called from YAML callbacks or internally)
    shelly_reachable_ = reachable;
    if (reachable) {
        shelly_uptime_seconds_ = uptime;
        shelly_last_ping_ms_ = esphome::millis();
    }
    ESP_LOGI(TAG, "Shelly health updated: %s (uptime: %us)",
             reachable ? "ONLINE" : "OFFLINE", uptime);
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
