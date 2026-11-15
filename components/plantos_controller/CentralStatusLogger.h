#ifndef CENTRAL_STATUS_LOGGER_H
#define CENTRAL_STATUS_LOGGER_H

// The controller's alert book plus the I2C scan result that i2c_scanner reports into it.
// The status report itself is PlantOSController::printStatusReport() (status_report.cpp), which
// reads everything else from the controller and the HAL at print time.
// Semantics pinned by tests/test_alerts.cpp.

#include "esphome/core/hal.h"
#include <cstdint>
#include <string>
#include <vector>

enum class AlertStatus : uint8_t {
    ACTIVE,
    RESOLVED  // kept in the list (see SUSPECT notes in tests/test_alerts.cpp)
};

struct Alert {
    std::string type;         // e.g. "PH_CRITICAL", "SHELLY_OFFLINE"
    std::string reason;       // short text shown in the report
    std::string user_action;  // next step for the operator (context alerts only)
    uint32_t timestamp;       // millis() when raised (updateAlertStatus also refreshes it)
    AlertStatus status{AlertStatus::ACTIVE};

    Alert(const std::string &t, const std::string &r, const std::string &action = std::string())
        : type(t), reason(r), user_action(action), timestamp(esphome::millis()) {}
};

// One I2C device as i2c_scanner reports it (interface to components/i2c_scanner).
struct I2CDeviceInfo {
    uint8_t address;
    std::string name;
    bool found;
    bool critical;

    I2CDeviceInfo(uint8_t addr, const std::string &n, bool f, bool c) : address(addr), name(n), found(f), critical(c) {}
};

class CentralStatusLogger {
public:
    // Raise or update an alert (reason and age refreshed); an empty reason clears it.
    void updateAlertStatus(const std::string &alertType, const std::string &reason = "");
    // Raise an alert with context. The cause / context / recovery texts go to the log once when the
    // alert is new; only reason and user action are kept for the report.
    void updateAlertWithContext(const std::string &alertType, const std::string &reason,
                                const std::string &root_cause, const std::string &user_action,
                                const std::string &operation_context, const std::string &recovery_plan,
                                uint8_t max_retries = 0);
    // Mark the first active alert of this type resolved (kept in the list).
    void resolveAlert(const std::string &alertType);
    // Remove every alert of this type (active or resolved).
    void clearAlert(const std::string &alertType);

    std::vector<Alert> getActiveAlerts() const;
    // true if the list has any entry, resolved ones included (pinned behavior)
    bool hasActiveAlerts() const { return !alerts_.empty(); }
    // "Alert State" web UI text: "Clear", "pH Critical", "Temp High (+1 more)", ...
    std::string getAlertStateShort() const;
    // Report lines for the active alerts ("Alerts: none" when there are none)
    void logActiveAlerts(const char *tag) const;

    // From i2c_scanner (after each scan)
    void updateI2CHardwareStatus(const std::vector<I2CDeviceInfo> &devices);
    // Report line part: "I2C 0x76 BME280 ok" (empty before the first scan)
    void formatI2C(char *buf, size_t size) const;

private:
    std::vector<Alert> alerts_;
    std::vector<I2CDeviceInfo> i2c_devices_;
    bool i2c_scanned_{false};
};

#endif  // CENTRAL_STATUS_LOGGER_H
