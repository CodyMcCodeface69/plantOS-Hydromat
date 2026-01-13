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
 * AlertStatus - Tracks whether an alert is active or resolved
 */
enum class AlertStatus {
    ACTIVE,      // Alert is currently active
    RESOLVED     // Alert was resolved but kept for history
};

/**
 * Alert structure to support multiple simultaneous alerts with enhanced error context
 */
struct Alert {
    // Existing fields (backward compatible)
    std::string type;        // e.g., "SPILL", "PH_CRITICAL", "TEMPERATURE"
    std::string reason;      // Brief reason (backward compat)
    uint32_t timestamp;      // When triggered (milliseconds)

    // NEW: Status tracking
    AlertStatus status;            // ACTIVE or RESOLVED
    uint32_t resolved_timestamp;   // When resolved (0 if not resolved)

    // NEW: Comprehensive error context (4-part error messages)
    std::string root_cause;        // Technical details: "SafetyGate rejected: Duration 45s > max 30s"
    std::string user_action;       // User-friendly next steps: "Reduce pH dose or increase max duration"
    std::string operation_context; // Operation state: "pH correction attempt 3/5, injection phase"
    std::string recovery_plan;     // Recovery recommendations: "System will retry with adapted duration"

    // NEW: Retry tracking
    uint8_t retry_count;           // How many times we retried this error
    uint8_t max_retries;           // Maximum retry attempts configured

    // Backward compatible constructor
    Alert(const std::string& t, const std::string& r)
        : type(t), reason(r), timestamp(esphome::millis()),
          status(AlertStatus::ACTIVE), resolved_timestamp(0),
          root_cause(""), user_action(""), operation_context(""), recovery_plan(""),
          retry_count(0), max_retries(0) {}

    // NEW: Constructor with full error context
    Alert(const std::string& t, const std::string& r,
          const std::string& root, const std::string& action,
          const std::string& context, const std::string& recovery,
          uint8_t max_retry = 0)
        : type(t), reason(r), timestamp(esphome::millis()),
          status(AlertStatus::ACTIVE), resolved_timestamp(0),
          root_cause(root), user_action(action),
          operation_context(context), recovery_plan(recovery),
          retry_count(0), max_retries(max_retry) {}

    // Calculate how long alert was active (ms)
    uint32_t getActiveDuration() const {
        if (resolved_timestamp > 0) {
            return resolved_timestamp - timestamp;
        }
        return esphome::millis() - timestamp;  // Still active
    }

    // Get duration in seconds for display
    float getActiveDurationSeconds() const {
        return getActiveDuration() / 1000.0f;
    }
};

/**
 * I²C Device Info structure for hardware status tracking
 */
struct I2CDeviceInfo {
    uint8_t address;         // I²C address (7-bit)
    std::string name;        // Device name
    bool found;              // Whether device was found during scan
    bool critical;           // Whether device is critical for operation

    I2CDeviceInfo(uint8_t addr, const std::string& n, bool f, bool c)
        : address(addr), name(n), found(f), critical(c) {}
};

/**
 * UART Device Info structure for hardware status tracking
 */
struct UARTDeviceInfo {
    std::string name;        // Device name (e.g., "EZO pH Sensor")
    std::string port;        // UART port (e.g., "UART1", "TX=GPIO4, RX=GPIO5")
    bool ready;              // Whether device is ready and responding
    bool critical;           // Whether device is critical for operation
    std::string status;      // Additional status info (e.g., "Calibrated", "Not responding")

    UARTDeviceInfo(const std::string& n, const std::string& p, bool r, bool c, const std::string& s = "")
        : name(n), port(p), ready(r), critical(c), status(s) {}
};

/**
 * 1-Wire Device Info structure for hardware status tracking
 */
struct OneWireDeviceInfo {
    std::string name;        // Device name (e.g., "DS18B20 Temperature")
    std::string port;        // GPIO pin (e.g., "GPIO23")
    bool ready;              // Whether device is ready and responding
    bool critical;           // Whether device is critical for operation
    std::string status;      // Additional status info (e.g., "22.5°C", "Not responding")

    OneWireDeviceInfo(const std::string& n, const std::string& p, bool r, bool c, const std::string& s = "")
        : name(n), port(p), ready(r), critical(c), status(s) {}
};

/**
 * Pump Configuration Info structure for calibration status tracking
 */
struct PumpConfigInfo {
    std::string pump_name;      // Pump name (e.g., "pH Pump", "Grow Pump")
    float flow_rate_ml_s;       // Flow rate in mL/second at configured PWM
    float pwm_intensity;        // PWM intensity (0.0-1.0)

    PumpConfigInfo(const std::string& name, float flow_rate, float pwm)
        : pump_name(name), flow_rate_ml_s(flow_rate), pwm_intensity(pwm) {}
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
     * Update water temperature reading
     * @param temp Current water temperature in °C
     * @param available Whether temperature reading is available
     */
    void updateWaterTemperature(float temp, bool available);

    /**
     * Update water level sensor states (3-sensor system)
     * @param high_sensor State of HIGH water level sensor (true = water present)
     * @param low_sensor State of LOW water level sensor (true = water present)
     * @param empty_sensor State of EMPTY water level sensor (true = water present)
     * @param available Whether all water level sensors are configured
     */
    void updateWaterLevelSensors(bool high_sensor, bool low_sensor, bool empty_sensor, bool available);

    /**
     * Update unified controller state
     * @param state Current controller state (INIT, IDLE, PH_MEASURING, FEEDING, etc.)
     */
    void updateControllerState(const std::string& state);
    void updateControllerState(const char* state);

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
    void updatePSMEvent(const std::string& eventID, int status, int64_t ageSeconds);
    void updatePSMEvent(const char* eventID, int status, int64_t ageSeconds);

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
     * Mark an alert as resolved (don't clear it, keep history)
     *
     * @param alertType The alert type to mark as resolved
     *
     * This transitions the alert from ACTIVE to RESOLVED state, recording
     * the resolution timestamp. The alert remains in the list for debugging.
     */
    void resolveAlert(const std::string& alertType);
    void resolveAlert(const char* alertType);

    /**
     * Update alert with comprehensive error context
     *
     * @param alertType Type identifier for the alert
     * @param reason Brief reason (backward compat)
     * @param root_cause Technical details about what failed
     * @param user_action User-friendly next steps
     * @param operation_context Current operation state
     * @param recovery_plan What system will do to recover
     * @param max_retries Maximum retry attempts (0 for no retry)
     */
    void updateAlertWithContext(
        const std::string& alertType,
        const std::string& reason,
        const std::string& root_cause,
        const std::string& user_action,
        const std::string& operation_context,
        const std::string& recovery_plan,
        uint8_t max_retries = 0);

    /**
     * Increment retry count for an alert
     *
     * @param alertType The alert type to update
     */
    void incrementAlertRetry(const std::string& alertType);

    /**
     * Get active alerts only (filter out resolved)
     *
     * @return Vector of active alerts
     */
    std::vector<Alert> getActiveAlerts() const;

    /**
     * Get resolved alerts only (for history display)
     *
     * @return Vector of resolved alerts
     */
    std::vector<Alert> getResolvedAlerts() const;

    /**
     * Get count of resolved alerts
     *
     * @return Number of resolved alerts in history
     */
    int getResolvedAlertCount() const;

    /**
     * Clear resolved alerts older than specified time
     *
     * @param max_age_ms Maximum age in milliseconds (default: 1 hour)
     *
     * This prunes old resolved alerts to prevent memory growth.
     */
    void pruneResolvedAlerts(uint32_t max_age_ms = 3600000);

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
     * Update I²C hardware status from scanner
     * @param devices Vector of I2CDeviceInfo structures with scan results
     */
    void updateI2CHardwareStatus(const std::vector<I2CDeviceInfo>& devices);

    /**
     * Update UART hardware status
     * @param devices Vector of UARTDeviceInfo structures with device status
     */
    void updateUARTHardwareStatus(const std::vector<UARTDeviceInfo>& devices);

    /**
     * Update 1-Wire hardware status
     * @param devices Vector of OneWireDeviceInfo structures with device status
     */
    void updateOneWireHardwareStatus(const std::vector<OneWireDeviceInfo>& devices);

    /**
     * Update pump configuration status
     * @param configs Vector of PumpConfigInfo structures with pump calibration data
     */
    void updatePumpConfigurations(const std::vector<PumpConfigInfo>& configs);

    /**
     * Update calendar status
     * @param currentDay Current day in the grow cycle (1-120)
     * @param phMin Target pH minimum for current day
     * @param phMax Target pH maximum for current day
     * @param nutrientA Nutrient A dose in mL/L
     * @param nutrientB Nutrient B dose in mL/L
     * @param nutrientC Nutrient C dose in mL/L
     * @param safeMode Whether calendar automation is disabled
     */
    void updateCalendarStatus(uint8_t currentDay, float phMin, float phMax,
                             float nutrientA, float nutrientB, float nutrientC,
                             bool safeMode);

    /**
     * Set 420 mode for easter egg logging
     * @param enabled Whether 420 mode is enabled
     */
    void set420Mode(bool enabled);

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

    /**
     * Print 420 ASCII art
     * Public method to allow controller to trigger easter egg on demand
     */
    void print420Art();

    /**
     * Log water level status with ASCII art visualization (5-state system)
     * @param high_sensor State of HIGH water level sensor (true = water present)
     * @param low_sensor State of LOW water level sensor (true = water present)
     * @param empty_sensor State of EMPTY water level sensor (true = water present)
     * @param sensors_available Whether all sensors are configured and available
     */
    void logWaterLevelStatus(bool high_sensor, bool low_sensor, bool empty_sensor, bool sensors_available);

    /**
     * Configure status logger behavior
     * @param enableReports Enable/disable periodic status reports
     * @param reportIntervalMs Report interval in milliseconds
     * @param verboseMode Enable instant verbose logging (filters out LED changes)
     */
    void configure(bool enableReports, uint32_t reportIntervalMs, bool verboseMode);

    /**
     * Check if periodic status reports should be printed
     * @return true if enough time has elapsed since last report
     */
    bool shouldPrintStatusReport();

    /**
     * Check if verbose mode is enabled
     * @return true if verbose mode is enabled
     */
    bool isVerboseMode() const { return verboseMode_; }

private:
    // Core system variables
    std::string currentIP;
    uint32_t lastLogTimestamp;
    float filteredPH;
    std::string activeRoutine;

    // Additional sensor data
    float waterTemperature;              // Water temperature (DS18B20)
    bool waterTempAvailable;             // Whether temperature reading is available
    bool waterLevelHighSensor;           // HIGH water level sensor state
    bool waterLevelLowSensor;            // LOW water level sensor state
    bool waterLevelEmptySensor;          // EMPTY water level sensor state
    bool waterLevelSensorsAvailable;     // Whether all water level sensors are configured

    // Web server status
    bool webServerOnline;
    bool webServerClientConnected;

    // System state tracking
    std::string controllerState;         // Unified controller FSM state
    bool maintenanceMode;                // Maintenance/shutdown mode flag
    std::string psmEventID;              // Current PSM event ID (empty if none)
    int psmEventStatus;                  // PSM event status code
    int64_t psmEventAge;                 // PSM event age in seconds (-1 if no event)

    // Configuration
    bool enableReports_;                 // Enable/disable periodic status reports
    uint32_t reportInterval_;            // Report interval in milliseconds
    bool verboseMode_;                   // Enable instant verbose logging

    // Alert management - supports multiple simultaneous alerts
    std::vector<Alert> activeAlerts;

    // I²C Hardware status
    std::vector<I2CDeviceInfo> i2cDevices;
    bool i2cScanPerformed;

    // UART Hardware status
    std::vector<UARTDeviceInfo> uartDevices;
    bool uartStatusUpdated;

    // 1-Wire Hardware status
    std::vector<OneWireDeviceInfo> oneWireDevices;
    bool oneWireStatusUpdated;

    // Pump Configuration status
    std::vector<PumpConfigInfo> pumpConfigs;
    bool pumpConfigsUpdated;

    // Calendar status
    uint8_t calendarCurrentDay;
    float calendarPhMin;
    float calendarPhMax;
    float calendarNutrientA;
    float calendarNutrientB;
    float calendarNutrientC;
    bool calendarSafeMode;
    bool calendarStatusUpdated;

    // Easter egg mode
    bool mode420_;

    // Helper methods
    std::string getFormattedTime();
    void printAlertBanner();
    void printSeparator(char c = '=', int length = 80);
};

#endif // CENTRAL_STATUS_LOGGER_H
