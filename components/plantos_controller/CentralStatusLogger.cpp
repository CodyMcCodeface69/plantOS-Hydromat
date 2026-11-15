#include "CentralStatusLogger.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

static const char *const TAG = "status.logger";

void CentralStatusLogger::updateAlertStatus(const std::string &alertType, const std::string &reason) {
    if (reason.empty()) {
        clearAlert(alertType);
        return;
    }
    // Matches resolved entries too: they stay resolved (pinned, see tests/test_alerts.cpp)
    for (auto &alert : alerts_) {
        if (alert.type == alertType) {
            alert.reason = reason;
            alert.timestamp = esphome::millis();
            return;
        }
    }
    alerts_.emplace_back(alertType, reason);
}

void CentralStatusLogger::updateAlertWithContext(const std::string &alertType, const std::string &reason,
                                                 const std::string &root_cause, const std::string &user_action,
                                                 const std::string &operation_context,
                                                 const std::string &recovery_plan, uint8_t) {
    for (auto &alert : alerts_) {
        if (alert.type == alertType && alert.status == AlertStatus::ACTIVE) {
            alert.reason = reason;
            alert.user_action = user_action;
            return;
        }
    }
    alerts_.emplace_back(alertType, reason, user_action);

    ESP_LOGE(TAG, "ALERT: %s - %s", alertType.c_str(), reason.c_str());
    if (!root_cause.empty()) ESP_LOGI(TAG, "  Root Cause: %s", root_cause.c_str());
    if (!user_action.empty()) ESP_LOGI(TAG, "  Next Steps: %s", user_action.c_str());
    if (!operation_context.empty()) ESP_LOGI(TAG, "  Context: %s", operation_context.c_str());
    if (!recovery_plan.empty()) ESP_LOGI(TAG, "  Recovery: %s", recovery_plan.c_str());
}

void CentralStatusLogger::resolveAlert(const std::string &alertType) {
    for (auto &alert : alerts_) {
        if (alert.type == alertType && alert.status == AlertStatus::ACTIVE) {
            alert.status = AlertStatus::RESOLVED;
            ESP_LOGI(TAG, "[RESOLVED] %s (was active for %.1f seconds)", alertType.c_str(),
                     (esphome::millis() - alert.timestamp) / 1000.0f);
            return;
        }
    }
}

void CentralStatusLogger::clearAlert(const std::string &alertType) {
    alerts_.erase(std::remove_if(alerts_.begin(), alerts_.end(),
                                 [&alertType](const Alert &a) { return a.type == alertType; }),
                  alerts_.end());
}

std::vector<Alert> CentralStatusLogger::getActiveAlerts() const {
    std::vector<Alert> active;
    for (const auto &alert : alerts_) {
        if (alert.status == AlertStatus::ACTIVE) active.push_back(alert);
    }
    return active;
}

namespace {

struct ShortName {
    const char *type;
    const char *text;
    bool prefix;
};

// Web UI "Alert State" texts. Unknown types show with '_' replaced by ' '.
const ShortName SHORT_NAMES[] = {
    {"PH_CRITICAL", "pH Critical", false},
    {"PH_SENSOR_CRITICAL", "pH Sensor Fail", false},
    {"PH_SENSOR_HARDWARE_FAILURE", "pH Hardware Fail", false},
    {"NO_PH_READINGS", "No pH Readings", false},
    {"PH_RANGE", "pH Out of Range", false},
    {"CALIBRATION_FAILED", "Calibration Fail", true},
    {"PUMP_REJECTION_ACID", "Pump Reject: Acid", false},
    {"PUMP_REJECTION_WATER_VALVE", "Pump Reject: Water", false},
    {"PUMP_REJECTION_WASTEWATER", "Pump Reject: Waste", false},
    {"PUMP_REJECTION_", "Pump Rejected", true},
    {"PUMP_FAILURE", "Pump Failure", false},
    {"SAFETY_GATE_REJECT", "Safety Reject", true},
    {"DURATION_ADAPTED", "Duration Adapted", true},
    {"HARDWARE_WATER_SENSOR_HIGH", "Water Sensor: High", false},
    {"HARDWARE_WATER_SENSOR_EMPTY", "Water Sensor: Empty", false},
    {"HARDWARE_WATER_SENSORS_MISSING", "Water Sensors Missing", false},
    {"WATER_LOW", "Water Low", false},
    {"HARDWARE_HAL_MISSING", "HAL Missing", false},
    {"SHELLY_OFFLINE", "Shelly Offline", false},
    {"TEMPERATURE", "Temp High", false},
    {"TEMP_HIGH", "Temp High", false},
    {"SPILL", "Spill Detected", false},
    {"PSM_TEST_OK", "PSM Test OK", false},
    {"PSM_TEST_WARNING", "PSM Warning", false},
    {"PSM_RECOVERY", "PSM Recovery", false},
    {"PSM_TEST_UNPLUG", "PSM Test Active", false},
};

std::string shortName(const std::string &type) {
    for (const auto &n : SHORT_NAMES) {
        if (n.prefix ? type.compare(0, strlen(n.type), n.type) == 0 : type == n.type) return n.text;
    }
    std::string result = type;
    std::replace(result.begin(), result.end(), '_', ' ');
    return result;
}

}  // namespace

std::string CentralStatusLogger::getAlertStateShort() const {
    const Alert *first = nullptr;
    size_t count = 0;
    for (const auto &alert : alerts_) {
        if (alert.status != AlertStatus::ACTIVE) continue;
        if (!first) first = &alert;
        count++;
    }
    if (!first) return "Clear";
    std::string text = shortName(first->type);
    if (count > 1) {
        char buf[16];
        snprintf(buf, sizeof(buf), " (+%u more)", (unsigned) (count - 1));
        text += buf;
    }
    return text;
}

void CentralStatusLogger::logActiveAlerts(const char *tag) const {
    unsigned count = 0;
    for (const auto &alert : alerts_) count += alert.status == AlertStatus::ACTIVE;
    if (count == 0) {
        ESP_LOGI(tag, "Alerts: none");
        return;
    }
    ESP_LOGW(tag, "Alerts (%u):", count);
    uint32_t now = esphome::millis();
    unsigned n = 0;
    for (const auto &alert : alerts_) {
        if (alert.status != AlertStatus::ACTIVE) continue;
        unsigned age = (unsigned) ((now - alert.timestamp) / 1000);
        ESP_LOGW(tag, "  [%u] %s: %s (%u s)%s%s", ++n, alert.type.c_str(), alert.reason.c_str(), age,
                 alert.user_action.empty() ? "" : " - ", alert.user_action.c_str());
    }
}

void CentralStatusLogger::updateI2CHardwareStatus(const std::vector<I2CDeviceInfo> &devices) {
    i2c_devices_ = devices;
    i2c_scanned_ = true;
}

void CentralStatusLogger::formatI2C(char *buf, size_t size) const {
    if (size == 0) return;
    buf[0] = '\0';
    if (!i2c_scanned_) {
        snprintf(buf, size, "I2C not scanned");
        return;
    }
    size_t len = snprintf(buf, size, "I2C");
    for (const auto &d : i2c_devices_) {
        if (!d.found && !d.critical) continue;
        if (len >= size) break;
        len += snprintf(buf + len, size - len, " 0x%02X %s %s", d.address, d.name.c_str(), d.found ? "ok" : "MISSING");
    }
    if (i2c_devices_.empty() && len < size) snprintf(buf + len, size - len, " none found");
}
