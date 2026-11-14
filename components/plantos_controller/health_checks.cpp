// Health checks: hardware and Shelly status, temperature alerts, sensor plausibility, calibration reminders, operation retry bookkeeping.
// Part of PlantOSController (declared in controller.h). FSMINFO.md is authoritative for FSM behavior.
#include "controller.h"
#include "esphome/components/plantos_hal/hal.h"
#include "esphome/components/actuator_safety_gate/ActuatorSafetyGate.h"
#include "esphome/components/persistent_state_manager/persistent_state_manager.h"
#include "esphome/components/calendar_manager/CalendarManager.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/alert_service/alert_service.h"
#include <algorithm>
#include <cmath>

namespace plantos_controller {

// ============================================================================
// Hardware detection and monitoring
// ============================================================================

void PlantOSController::checkShellyHealth() {
    if (!hal_) {
        return;
    }

    uint32_t now = hal_->getSystemTime();

    // Only update status logger every 30 seconds
    if (now - last_shelly_check_time_ < SHELLY_CHECK_INTERVAL) {
        return;
    }
    last_shelly_check_time_ = now;

    // Shelly switched off on purpose: report it as disabled, not as an offline alert
    if (!hal_->isShellyEnabled()) {
        std::vector<ShellyDeviceInfo> devices;
        devices.push_back(ShellyDeviceInfo("Shelly Plus 4PM", "192.168.0.130", false, false, "Disabled"));
        status_logger_.updateShellyHardwareStatus(devices);
        status_logger_.clearAlert("SHELLY_OFFLINE");
        return;
    }

    // Read cached Shelly health status from HAL (polled by the HAL's Shelly worker task)
    bool reachable = hal_->isShellyReachable();
    uint32_t uptime = hal_->getShellyUptime();

    ESP_LOGD(TAG, "Shelly health status: %s (uptime: %us)",
             reachable ? "ONLINE" : "OFFLINE", uptime);

    // Update CentralStatusLogger with result
    std::vector<ShellyDeviceInfo> devices;
    devices.push_back(ShellyDeviceInfo(
        "Shelly Plus 4PM",
        "192.168.0.130",
        reachable,
        true,  // Critical device
        reachable ? "Online" : "Offline"
    ));
    if (reachable) {
        devices[0].uptime_seconds = uptime;
        devices[0].last_seen_ms = now;
    }
    status_logger_.updateShellyHardwareStatus(devices);

    // Update alert status based on reachability
    if (!reachable) {
        status_logger_.updateAlertStatus("SHELLY_OFFLINE",
            "Shelly Plus 4PM not responding to HTTP ping");
    } else {
        status_logger_.clearAlert("SHELLY_OFFLINE");
    }
}

// ============================================================================
// Sensor health checks
// ============================================================================

void PlantOSController::checkTemperatureAlerts() {
    if (!hal_ || !hal_->hasTemperature()) return;

    float temp = hal_->readTemperature();

    if (temp > 32.0f) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Water temp CRITICAL: %.1f\xC2\xB0""C (limit: >32\xC2\xB0""C)", temp);
        status_logger_.updateAlertStatus("TEMP_CRITICAL", msg);
        status_logger_.clearAlert("TEMP_HIGH");
        status_logger_.clearAlert("TEMP_LOW");
        ESP_LOGE(TAG, "[TEMP_ALERT] %s", msg);
        temp_alert_active_ = true;
    } else if (temp > 28.0f) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Water temp high: %.1f\xC2\xB0""C (warn: >28\xC2\xB0""C)", temp);
        status_logger_.updateAlertStatus("TEMP_HIGH", msg);
        status_logger_.clearAlert("TEMP_CRITICAL");
        status_logger_.clearAlert("TEMP_LOW");
        ESP_LOGW(TAG, "[TEMP_ALERT] %s", msg);
        temp_alert_active_ = true;
    } else if (temp < 15.0f) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Water temp low: %.1f\xC2\xB0""C (warn: <15\xC2\xB0""C)", temp);
        status_logger_.updateAlertStatus("TEMP_LOW", msg);
        status_logger_.clearAlert("TEMP_CRITICAL");
        status_logger_.clearAlert("TEMP_HIGH");
        ESP_LOGW(TAG, "[TEMP_ALERT] %s", msg);
        temp_alert_active_ = true;
    } else {
        status_logger_.clearAlert("TEMP_CRITICAL");
        status_logger_.clearAlert("TEMP_HIGH");
        status_logger_.clearAlert("TEMP_LOW");
        if (temp_alert_active_) {
            ESP_LOGI(TAG, "[TEMP_ALERT] Temperature back in normal range: %.1f\xC2\xB0""C", temp);
        }
        temp_alert_active_ = false;
    }
}

void PlantOSController::checkSensorPlausibility() {
    if (!hal_) return;

    uint32_t now = hal_->getSystemTime();

    // pH plausibility checks
    if (hal_->hasPhValue()) {
        float ph = hal_->readPH();

        // Range check: valid pH 3.0–10.0
        if (ph < 3.0f || ph > 10.0f) {
            char msg[80];
            snprintf(msg, sizeof(msg), "pH %.2f outside valid range [3.0-10.0] - sensor error?", ph);
            status_logger_.updateAlertStatus("PH_PLAUSIBILITY", msg);
            ESP_LOGE(TAG, "[PLAUSIBILITY] %s", msg);
        } else {
            // Rate-of-change check: > 1.0 pH/min = likely sensor error
            if (last_ph_plausibility_time_ > 0 && hal_->hasTime()) {
                int64_t current_ts = hal_->getCurrentTimestamp();
                int64_t dt_s = current_ts - last_ph_plausibility_time_;
                // Only check if interval is sane (5s–120s)
                if (dt_s >= 5 && dt_s <= 120) {
                    float rate = std::abs(ph - last_ph_plausibility_value_) / (dt_s / 60.0f);
                    if (rate > 1.0f) {
                        char msg[100];
                        snprintf(msg, sizeof(msg),
                                 "pH change %.2f pH/min exceeds 1.0/min limit (%.2f→%.2f in %llds)",
                                 rate, last_ph_plausibility_value_, ph, (long long)dt_s);
                        status_logger_.updateAlertStatus("PH_PLAUSIBILITY", msg);
                        ESP_LOGW(TAG, "[PLAUSIBILITY] %s", msg);
                    } else {
                        status_logger_.clearAlert("PH_PLAUSIBILITY");
                    }
                }
            } else {
                status_logger_.clearAlert("PH_PLAUSIBILITY");
            }

            // Update tracking values
            last_ph_plausibility_value_ = ph;
            last_ph_plausibility_time_ = hal_->hasTime() ? hal_->getCurrentTimestamp()
                                                         : (int64_t)(now / 1000);
        }
    }

    // EC plausibility checks
    if (hal_->hasECValue()) {
        float ec = hal_->readEC();  // µS/cm

        // Range check: 0–5000 µS/cm
        if (ec < 0.0f || ec > 5000.0f) {
            char msg[80];
            snprintf(msg, sizeof(msg), "EC %.1f uS/cm outside valid range [0-5000] - sensor error?", ec);
            status_logger_.updateAlertStatus("EC_PLAUSIBILITY", msg);
            ESP_LOGE(TAG, "[PLAUSIBILITY] %s", msg);
        } else {
            // Jump check: > 1000 µS/cm between consecutive 1-min checks = suspicious
            if (last_ec_plausibility_value_ >= 0.0f) {
                float jump = std::abs(ec - last_ec_plausibility_value_);
                if (jump > 1000.0f) {
                    char msg[100];
                    snprintf(msg, sizeof(msg),
                             "EC jump %.1f uS/cm between checks (%.1f→%.1f) exceeds 1000 uS/cm limit",
                             jump, last_ec_plausibility_value_, ec);
                    status_logger_.updateAlertStatus("EC_PLAUSIBILITY", msg);
                    ESP_LOGW(TAG, "[PLAUSIBILITY] %s", msg);
                } else {
                    status_logger_.clearAlert("EC_PLAUSIBILITY");
                }
            }
            last_ec_plausibility_value_ = ec;
        }
    }
}

void PlantOSController::checkWaterLevelPlausibility() {
    if (!hal_ || !hal_->hasWaterLevelSensors()) return;

    bool high = hal_->readWaterLevelHigh();
    bool empty = hal_->readWaterLevelEmpty();

    // EMPTY is the lowest sensor (~bottom of tank) — always ON when there's any water.
    // HIGH=ON + EMPTY=ON is normal (full tank). The impossible state is HIGH=ON + EMPTY=OFF.
    if (high && !empty) {
        status_logger_.updateAlertStatus("WATER_LEVEL_LOGIC_ERROR",
            "Water level sensors contradictory: HIGH active but EMPTY inactive (sensor fault?)");
        ESP_LOGE(TAG, "[PLAUSIBILITY] Water level logic error: HIGH=ON but EMPTY=OFF — possible sensor fault");
    } else {
        status_logger_.clearAlert("WATER_LEVEL_LOGIC_ERROR");
    }
}

void PlantOSController::checkCalibrationReminders() {
    if (!hal_ || !hal_->hasTime()) return;

    int64_t current_ts = hal_->getCurrentTimestamp();
    if (current_ts <= 0) return;

    // pH calibration reminder (every 14 days)
    if (last_ph_calibration_ts_ > 0) {
        int64_t elapsed_s = current_ts - last_ph_calibration_ts_;
        if (elapsed_s >= PH_CALIB_INTERVAL_S) {
            int64_t days = elapsed_s / 86400;
            char msg[80];
            snprintf(msg, sizeof(msg), "pH calibration overdue: %lld days since last calibration", (long long)days);
            status_logger_.updateAlertStatus("PH_CALIBRATION_DUE", msg);
            ESP_LOGW(TAG, "[CALIB_REMINDER] %s", msg);
        } else {
            status_logger_.clearAlert("PH_CALIBRATION_DUE");
        }
    }

    // EC calibration reminder (every 30 days)
    if (last_ec_calibration_ts_ > 0) {
        int64_t elapsed_s = current_ts - last_ec_calibration_ts_;
        if (elapsed_s >= EC_CALIB_INTERVAL_S) {
            int64_t days = elapsed_s / 86400;
            char msg[80];
            snprintf(msg, sizeof(msg), "EC calibration overdue: %lld days since last calibration", (long long)days);
            status_logger_.updateAlertStatus("EC_CALIBRATION_DUE", msg);
            ESP_LOGW(TAG, "[CALIB_REMINDER] %s", msg);
        } else {
            status_logger_.clearAlert("EC_CALIBRATION_DUE");
        }
    }
}

} // namespace plantos_controller
