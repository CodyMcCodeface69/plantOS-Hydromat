#ifndef CENTRAL_STATUS_LOGGER_H
#define CENTRAL_STATUS_LOGGER_H

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <string>
#include <vector>
#include <algorithm>
#include <ctime>
#include <cstdio>

/**
 * Alert structure to support multiple simultaneous alerts
 */
struct Alert {
    std::string type;        // e.g., "SPILL", "PH_CRITICAL", "TEMPERATURE"
    std::string reason;      // Detailed reason for the alert
    uint32_t timestamp;      // When the alert was triggered

    Alert(const std::string& t, const std::string& r)
        : type(t), reason(r), timestamp(esphome::millis()) {}
};

/**
 * CentralStatusLogger - Unified logging and status display system
 *
 * This class manages all critical system variables and provides
 * structured logging output with support for multiple simultaneous alerts.
 */
class CentralStatusLogger {
public:
    CentralStatusLogger();

    /**
     * Initialize the logger and set initial values
     */
    void begin();

    /**
     * Update sensor readings and routine state
     * @param ph Current filtered pH reading
     * @param routine Name of currently running routine
     */
    void updateStatus(float ph, const std::string& routine);
    void updateStatus(float ph, const char* routine);

    /**
     * Add or clear alert status
     * @param alertType Type identifier for the alert (e.g., "SPILL", "PH_CRITICAL")
     * @param reason Detailed reason for the alert (empty to clear)
     */
    void updateAlertStatus(const std::string& alertType, const std::string& reason = "");
    void updateAlertStatus(const char* alertType, const std::string& reason = "");

    /**
     * Clear a specific alert by type
     * @param alertType The alert type to clear
     */
    void clearAlert(const std::string& alertType);
    void clearAlert(const char* alertType);

    /**
     * Clear all active alerts
     */
    void clearAllAlerts();

    /**
     * Update IP address from WiFi/Network component
     * @param ip Current IP address as string
     */
    void updateIP(const std::string& ip);
    void updateIP(const char* ip);

    /**
     * Update web server status
     * @param online Whether the web server is running
     * @param clientConnected Whether a client is currently connected
     */
    void updateWebServerStatus(bool online, bool clientConnected);

    /**
     * Print complete structured status summary to Serial
     * This is the primary logging method that outputs all system state
     */
    void logStatus();

    /**
     * Check if any alerts are currently active
     * @return true if one or more alerts are active
     */
    bool hasActiveAlerts() const;

    /**
     * Get count of active alerts
     * @return Number of active alerts
     */
    int getAlertCount() const;

private:
    // Core system variables
    std::string currentIP;
    uint32_t lastLogTimestamp;
    float filteredPH;
    std::string activeRoutine;

    // Web server status
    bool webServerOnline;
    bool webServerClientConnected;

    // Alert management - supports multiple simultaneous alerts
    std::vector<Alert> activeAlerts;

    // Helper methods
    std::string getFormattedTime();
    void printAlertBanner();
    void printSeparator(char c = '=', int length = 80);
};

#endif // CENTRAL_STATUS_LOGGER_H
