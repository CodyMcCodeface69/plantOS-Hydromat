#ifndef CENTRAL_STATUS_LOGGER_H
#define CENTRAL_STATUS_LOGGER_H

#include <Arduino.h>
#include <vector>
#include <time.h>

/**
 * Alert structure to support multiple simultaneous alerts
 */
struct Alert {
    String type;        // e.g., "SPILL", "PH_CRITICAL", "TEMPERATURE"
    String reason;      // Detailed reason for the alert
    unsigned long timestamp;  // When the alert was triggered

    Alert(const String& t, const String& r) : type(t), reason(r), timestamp(millis()) {}
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
    void updateStatus(float ph, const String& routine);

    /**
     * Update Controller FSM state (hardware-level state machine)
     * @param state Current Controller state (INIT, CALIBRATION, READY, ERROR, etc.)
     */
    void updateControllerState(const String& state);

    /**
     * Update PlantOS Logic state (application-level state machine)
     * @param state Current PlantOS Logic state (IDLE, PH_MEASURING, FEEDING_INJECTING, etc.)
     */
    void updateLogicState(const String& state);

    /**
     * Update maintenance mode status
     * @param enabled True if maintenance mode is active
     */
    void updateMaintenanceMode(bool enabled);

    /**
     * Update PSM (Persistent State Manager) event status
     * @param eventID Current event ID (empty string if no event)
     * @param status Event status code
     * @param ageSeconds Age of event in seconds (-1 if no event)
     */
    void updatePSMEvent(const String& eventID, int status, int64_t ageSeconds);

    /**
     * Add or clear alert status
     * @param alertType Type identifier for the alert (e.g., "SPILL", "PH_CRITICAL")
     * @param reason Detailed reason for the alert (empty to clear)
     */
    void updateAlertStatus(const String& alertType, const String& reason = "");

    /**
     * Clear a specific alert by type
     * @param alertType The alert type to clear
     */
    void clearAlert(const String& alertType);

    /**
     * Clear all active alerts
     */
    void clearAllAlerts();

    /**
     * Update IP address from WiFi/Network component
     * @param ip Current IP address as string
     */
    void updateIP(const String& ip);

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
    String currentIP;
    long lastLogTimestamp;
    float filteredPH;
    String activeRoutine;

    // Web server status
    bool webServerOnline;
    bool webServerClientConnected;

    // System state tracking
    String controllerState;         // Controller FSM state
    String logicState;              // PlantOS Logic FSM state
    bool maintenanceMode;           // Maintenance/shutdown mode flag
    String psmEventID;              // Current PSM event ID (empty if none)
    int psmEventStatus;             // PSM event status code
    int64_t psmEventAge;            // PSM event age in seconds (-1 if no event)

    // Alert management - supports multiple simultaneous alerts
    std::vector<Alert> activeAlerts;

    // Helper methods
    String getFormattedTime();
    void printAlertBanner();
    void printSeparator(char c = '=', int length = 80);
};

#endif // CENTRAL_STATUS_LOGGER_H
