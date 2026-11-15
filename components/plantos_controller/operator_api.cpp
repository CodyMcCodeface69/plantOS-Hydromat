// Operator API: the calls behind the web UI buttons, switches, numbers and text sensors in
// plantOS.yaml. Each YAML lambda is one call into this file (or a start*/set* method), so
// the logic and the logging live here and actuator commands go Controller -> SafetyGate -> HAL.
// Part of PlantOSController (declared in controller.h).
#include "controller.h"
#include "esphome/components/plantos_hal/hal.h"
#include "esphome/components/actuator_safety_gate/ActuatorSafetyGate.h"
#include "esphome/components/calendar_manager/CalendarManager.h"
#include "esphome/components/number/number.h"
#include "esphome/components/time/real_time_clock.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace plantos_controller {

void PlantOSController::set_ph_k_number(esphome::number::Number* number) {
    if (!number) return;
    set_ph_k_update_callback([number](float k) { number->publish_state(k); });
}

// ============================================================================
// Manual actuator commands and timed tests
// ============================================================================

bool PlantOSController::commandActuator(const char* actuator_id, bool on) {
    // ON for an actuator the active preset lacks: refused here with the reason (the gate would
    // reject it too). OFF always passes. The gate logs approvals and rejections itself.
    if (on && !actuatorAvailable(actuator_id, actuator_id)) return false;
    return requestPump(actuator_id, on, 0);
}

void PlantOSController::startActuatorTest(const char* actuator_id, uint32_t run_ms, uint32_t gate_max_s) {
    ActuatorTest* slot = nullptr;
    for (auto& t : actuator_tests_) {
        if (t.id && strcmp(t.id, actuator_id) == 0) {
            ESP_LOGW(TAG, "Test %s already running", actuator_id);
            return;
        }
        if (!t.id && !slot) slot = &t;
    }
    if (!slot) {
        ESP_LOGW(TAG, "Test %s refused: too many tests running", actuator_id);
        return;
    }
    if (!actuatorAvailable(actuator_id, "Actuator test")) return;
    if (!requestPump(actuator_id, true, gate_max_s)) {
        ESP_LOGW(TAG, "Test %s rejected by safety gate", actuator_id);
        return;
    }
    slot->id = actuator_id;
    slot->start_ms = esphome::millis();
    slot->run_ms = run_ms;
    ESP_LOGI(TAG, "Test %s ON for %.2f s", actuator_id, run_ms / 1000.0f);
}

void PlantOSController::startPumpTest(const char* pump_id) {
    if (hal_) {
        ESP_LOGI(TAG, "Pump test %s: 10 s at %.0f%% PWM, measure the volume (mL / 10 = mL/s)", pump_id,
                 hal_->getPumpConfig(pump_id).pwm_intensity * 100.0f);
    }
    startActuatorTest(pump_id, 10000, 10);
}

void PlantOSController::startPumpDoseTest(const char* pump_id, float ml) {
    if (!hal_) return;
    float duration_s = hal_->pumpflow(pump_id, ml);
    if (!(duration_s > 0.0f)) {
        ESP_LOGW(TAG, "Dose test %s: no valid duration for %.1f mL", pump_id, ml);
        return;
    }
    uint32_t run_ms = static_cast<uint32_t>(duration_s * 1000.0f);
    ESP_LOGI(TAG, "Dose test %s: %.1f mL = %.2f s, check the dispensed volume", pump_id, ml, duration_s);
    startActuatorTest(pump_id, run_ms, static_cast<uint32_t>(std::ceil(duration_s)));
}

void PlantOSController::startValveTest() {
    startActuatorTest(WATER_VALVE, 3000, 10);
}

void PlantOSController::updateActuatorTests() {
    uint32_t now = esphome::millis();
    for (auto& t : actuator_tests_) {
        if (t.id && now - t.start_ms >= t.run_ms) {
            requestPump(t.id, false, 0);
            ESP_LOGI(TAG, "Test %s OFF after %.2f s", t.id, t.run_ms / 1000.0f);
            t = ActuatorTest{};
        }
    }
}

void PlantOSController::cancelActuatorTests() {
    // Called from turnOffAllPumps(), which switches everything off except the grow light
    for (auto& t : actuator_tests_) {
        if (t.id && strcmp(t.id, "GrowLight") != 0) t = ActuatorTest{};
    }
}

// ============================================================================
// Settings
// ============================================================================

void PlantOSController::setAirPumpCycling(bool enabled, float on_period_s, float off_period_s) {
    if (!safety_gate_) return;
    if (!profile_.has(preset_model::FN_AIR_PUMP)) {
        // The gate stores the mode and sends nothing; it applies once a preset has the air pump
        ESP_LOGI(TAG, "Air pump cycling %s stored: preset %s has no air pump", enabled ? "ON" : "OFF",
                 profile_.active_name());
    }
    if (enabled) {
        uint32_t on_s = static_cast<uint32_t>(on_period_s);
        uint32_t off_s = static_cast<uint32_t>(off_period_s);
        ESP_LOGI(TAG, "Air pump cycling ON: %us ON / %us OFF", (unsigned)on_s, (unsigned)off_s);
        safety_gate_->setCyclingPeriods(AIR_PUMP, on_s, off_s);
        safety_gate_->enableCycling(AIR_PUMP, true);
    } else {
        ESP_LOGI(TAG, "Air pump cycling OFF (runs 24/7)");
        safety_gate_->enableCycling(AIR_PUMP, false);
    }
}

void PlantOSController::sendPhTempCompensationFromSensor() {
    if (!hal_ || !hal_->hasTemperature()) {
        ESP_LOGE(TAG, "pH temperature compensation: temperature sensor not available");
        return;
    }
    sendPhTempCompensation(hal_->readTemperature());
}

void PlantOSController::sendPhTempCompensation(float temp_c) {
    if (!hal_) return;
    if (hal_->sendPhTemperatureCompensation(temp_c)) {
        ESP_LOGI(TAG, "pH temperature compensation %.1f C sent", temp_c);
    } else {
        ESP_LOGE(TAG, "pH temperature compensation %.1f C failed", temp_c);
    }
}

void PlantOSController::forceTimeSync() {
    if (!time_source_) return;
    ESP_LOGW(TAG, "Force NTP sync: restarting SNTP (time valid before: %s)",
             time_source_->now().is_valid() ? "YES" : "NO");
    time_source_->setup();
}

void PlantOSController::startEcCalibration(float target_us_cm) {
    setEcCalibrationTarget(target_us_cm);
    startEcCalibration();
}

bool PlantOSController::changeGrowDay(int delta) {
    if (!calendar_manager_ || delta == 0) return false;
    bool ok = delta > 0 ? calendar_manager_->advance_day() : calendar_manager_->go_back_day();
    if (ok) {
        ESP_LOGI(TAG, "Grow day %s: now day %u", delta > 0 ? "+1" : "-1",
                 (unsigned)calendar_manager_->get_current_day());
    } else {
        ESP_LOGE(TAG, "Grow day %s failed", delta > 0 ? "+1" : "-1");
    }
    return ok;
}

void PlantOSController::onWifiConnected(const std::string& ip) {
    status_logger_.updateIP(ip);
    status_logger_.updateWebServerStatus(true, false);
}

// ============================================================================
// Text sensors
// ============================================================================

std::string PlantOSController::uptimeText() const {
    uint32_t sec = esphome::millis() / 1000;
    char buf[12];
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)(sec / 3600), (unsigned)((sec % 3600) / 60),
             (unsigned)(sec % 60));
    return buf;
}

std::string PlantOSController::tdsText() const {
    if (!hal_ || !hal_->hasECValue()) return "N/A";
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f ppm", hal_->readEC() * 0.5f);
    return buf;
}

std::string PlantOSController::tankVolumeText() const {
    if (!hal_) return "";
    char buf[80];
    snprintf(buf, sizeof(buf), "Delta: %.1fL (LOW→HIGH) | Total: %.1fL (EMPTY→HIGH)", hal_->getTankVolumeDelta(),
             hal_->getTotalTankVolume());
    return buf;
}

std::string PlantOSController::currentTimeText() const {
    if (!time_source_) return "Not initialized";
    auto time = time_source_->now();
    if (!time.is_valid()) return "Not initialized";
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", time.year, time.month, time.day_of_month, time.hour,
             time.minute, time.second);
    return buf;
}

std::string PlantOSController::growDayText() const {
    if (!calendar_manager_) return "";
    char buf[32];
    return preset_model::formatGrowDay(buf, sizeof(buf), calendar_manager_->get_current_day(),
                                       calendar_manager_->get_defined_days());
}

}  // namespace plantos_controller
