#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include <string>
#include <vector>

namespace alert_service {

/**
 * AlertLevel - Severity classification for alerts
 */
enum class AlertLevel {
    INFO,      // Informational events (e.g., system reboot, calibration saved)
    WARNING,   // Degraded state requiring attention (e.g., temp high, overdue calib)
    CRITICAL   // Immediate action required (e.g., sensor failed, temp CRITICAL)
};

/**
 * AlertBackend - Abstract interface for alert delivery
 *
 * Implement this to add new alert channels:
 *   - LogAlertBackend  (built-in): ESP_LOG output
 *   - TelegramAlertBackend (future): Telegram Bot API
 *   - HAAlertBackend       (future): Home Assistant notification service
 */
class AlertBackend {
public:
    virtual void sendAlert(AlertLevel level, const std::string& title, const std::string& msg) = 0;
    virtual ~AlertBackend() = default;
};

/**
 * LogAlertBackend - Delivers alerts via ESP_LOG
 *
 * INFO    → ESP_LOGI
 * WARNING → ESP_LOGW
 * CRITICAL→ ESP_LOGE
 */
class LogAlertBackend : public AlertBackend {
public:
    void sendAlert(AlertLevel level, const std::string& title, const std::string& msg) override;
};

/**
 * AlertService - Centralized alert dispatching with rate limiting
 *
 * Dispatches every alert to all registered backends.
 * Rate limiting: same alert title is suppressed for 30 minutes after the
 * first send to prevent log/notification spam during persistent faults.
 *
 * Usage:
 *   alert_service->alert(AlertLevel::WARNING, "Temp high", "28.3°C exceeds 28°C limit");
 *
 * The built-in LogAlertBackend is registered automatically in setup().
 * Additional backends can be registered via addBackend() after construction.
 */
class AlertService : public esphome::Component {
public:
    AlertService();

    void setup() override;

    /**
     * Register an alert delivery backend
     * Pointer ownership is NOT transferred - caller must keep it alive
     */
    void addBackend(AlertBackend* backend);

    /**
     * Send an alert to all registered backends (rate-limited per title)
     * @param level   Severity level
     * @param title   Short alert title (also used as rate-limit key)
     * @param msg     Detailed message body
     */
    void alert(AlertLevel level, const std::string& title, const std::string& msg);

    /**
     * Reset the rate-limit entry for a specific alert title
     * Useful after resolving an alert so it can fire again immediately
     */
    void resetRateLimit(const std::string& title);

private:
    static constexpr const char* TAG = "alert_service";
    static constexpr uint32_t RATE_LIMIT_MS = 1800000;  // 30 minutes

    LogAlertBackend log_backend_;
    std::vector<AlertBackend*> backends_;

    struct RateLimitEntry {
        std::string title;
        uint32_t last_sent_ms{0};
    };
    std::vector<RateLimitEntry> rate_limits_;

    bool isRateLimited(const std::string& title) const;
    void updateRateLimit(const std::string& title);
};

}  // namespace alert_service
