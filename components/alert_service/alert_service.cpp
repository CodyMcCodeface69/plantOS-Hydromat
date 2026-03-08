#include "alert_service.h"
#include "esphome/core/hal.h"  // for esphome::millis()

namespace alert_service {

// ============================================================================
// Helpers
// ============================================================================

static const char* level_name(AlertLevel level) {
    switch (level) {
        case AlertLevel::INFO:     return "INFO";
        case AlertLevel::WARNING:  return "WARNING";
        case AlertLevel::CRITICAL: return "CRITICAL";
        default:                   return "UNKNOWN";
    }
}

// ============================================================================
// LogAlertBackend
// ============================================================================

void LogAlertBackend::sendAlert(AlertLevel level, const std::string& title, const std::string& msg) {
    switch (level) {
        case AlertLevel::CRITICAL:
            ESP_LOGE("alert", "[ALERT/%s] %s — %s", level_name(level), title.c_str(), msg.c_str());
            break;
        case AlertLevel::WARNING:
            ESP_LOGW("alert", "[ALERT/%s] %s — %s", level_name(level), title.c_str(), msg.c_str());
            break;
        default:
            ESP_LOGI("alert", "[ALERT/%s] %s — %s", level_name(level), title.c_str(), msg.c_str());
            break;
    }
}

// ============================================================================
// AlertService
// ============================================================================

AlertService::AlertService() = default;

void AlertService::setup() {
    // Register built-in log backend as first backend
    backends_.push_back(&log_backend_);
    ESP_LOGI(TAG, "AlertService ready — LogAlertBackend registered");
}

void AlertService::addBackend(AlertBackend* backend) {
    if (backend) {
        backends_.push_back(backend);
        ESP_LOGI(TAG, "AlertService: backend registered (%zu total)", backends_.size());
    }
}

void AlertService::alert(AlertLevel level, const std::string& title, const std::string& msg) {
    if (isRateLimited(title)) {
        ESP_LOGD(TAG, "Alert '%s' suppressed (rate-limit: 30 min)", title.c_str());
        return;
    }

    updateRateLimit(title);

    for (auto* backend : backends_) {
        backend->sendAlert(level, title, msg);
    }
}

void AlertService::resetRateLimit(const std::string& title) {
    for (auto& entry : rate_limits_) {
        if (entry.title == title) {
            entry.last_sent_ms = 0;
            return;
        }
    }
}

bool AlertService::isRateLimited(const std::string& title) const {
    uint32_t now = esphome::millis();
    for (const auto& entry : rate_limits_) {
        if (entry.title == title) {
            return (now - entry.last_sent_ms) < RATE_LIMIT_MS;
        }
    }
    return false;  // No entry → not yet rate-limited
}

void AlertService::updateRateLimit(const std::string& title) {
    uint32_t now = esphome::millis();
    for (auto& entry : rate_limits_) {
        if (entry.title == title) {
            entry.last_sent_ms = now;
            return;
        }
    }
    rate_limits_.push_back({title, now});
}

}  // namespace alert_service
