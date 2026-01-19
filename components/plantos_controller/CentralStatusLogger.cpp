#include "CentralStatusLogger.h"

static const char *TAG = "status.logger";

CentralStatusLogger::CentralStatusLogger()
    : currentIP("0.0.0.0"),
      lastLogTimestamp(0),
      filteredPH(7.0),
      activeRoutine("INIT"),
      waterTemperature(0.0f),
      waterTempAvailable(false),
      waterLevelHighSensor(false),
      waterLevelLowSensor(false),
      waterLevelSensorsAvailable(false),
      webServerOnline(false),
      webServerClientConnected(false),
      controllerState("UNKNOWN"),
      maintenanceMode(false),
      psmEventID(""),
      psmEventStatus(0),
      psmEventAge(-1),
      enableReports_(true),
      reportInterval_(30000),
      verboseMode_(false),
      i2cScanPerformed(false),
      uartStatusUpdated(false),
      oneWireStatusUpdated(false),
      shellyStatusUpdated_(false),
      pumpConfigsUpdated(false),
      calendarCurrentDay(1),
      calendarPhMin(5.8f),
      calendarPhMax(6.2f),
      calendarNutrientA(0.0f),
      calendarNutrientB(0.0f),
      calendarNutrientC(0.0f),
      calendarSafeMode(false),
      calendarStatusUpdated(false),
      mode420_(true) {
}

void CentralStatusLogger::begin() {
    ESP_LOGI(TAG, "Initializing CentralStatusLogger...");

    // Set initial values
    currentIP = "0.0.0.0";
    lastLogTimestamp = esphome::millis();
    filteredPH = 7.0;
    activeRoutine = "IDLE";
    webServerOnline = false;
    webServerClientConnected = false;
    controllerState = "UNKNOWN";
    maintenanceMode = false;
    psmEventID = "";
    psmEventStatus = 0;
    psmEventAge = -1;
    activeAlerts.clear();

    printSeparator();
    ESP_LOGI(TAG, "  CENTRAL STATUS LOGGER INITIALIZED");
    ESP_LOGI(TAG, "  Status Reports: %s", enableReports_ ? "ENABLED" : "DISABLED");
    if (enableReports_) {
        ESP_LOGI(TAG, "  Report Interval: %u ms", reportInterval_);
    }
    ESP_LOGI(TAG, "  Verbose Mode: %s", verboseMode_ ? "ENABLED" : "DISABLED");
    printSeparator();
}

void CentralStatusLogger::updateStatus(float ph, const std::string& routine) {
    filteredPH = ph;
    activeRoutine = routine;
}

void CentralStatusLogger::updateStatus(float ph, const char* routine) {
    updateStatus(ph, std::string(routine));
}

void CentralStatusLogger::updateWaterTemperature(float temp, bool available) {
    waterTemperature = temp;
    waterTempAvailable = available;
}

void CentralStatusLogger::updateWaterLevelSensors(bool high_sensor, bool low_sensor, bool empty_sensor, bool available) {
    waterLevelHighSensor = high_sensor;
    waterLevelLowSensor = low_sensor;
    waterLevelEmptySensor = empty_sensor;
    waterLevelSensorsAvailable = available;
}

void CentralStatusLogger::updateControllerState(const std::string& state) {
    controllerState = state;
}

void CentralStatusLogger::updateControllerState(const char* state) {
    updateControllerState(std::string(state));
}


void CentralStatusLogger::updateMaintenanceMode(bool enabled) {
    maintenanceMode = enabled;
}

void CentralStatusLogger::updatePSMEvent(const std::string& eventID, int status, int64_t ageSeconds) {
    psmEventID = eventID;
    psmEventStatus = status;
    psmEventAge = ageSeconds;
}

void CentralStatusLogger::updatePSMEvent(const char* eventID, int status, int64_t ageSeconds) {
    updatePSMEvent(std::string(eventID), status, ageSeconds);
}

void CentralStatusLogger::updateAlertStatus(const std::string& alertType, const std::string& reason) {
    if (reason.empty()) {
        // Empty reason means clear this alert
        clearAlert(alertType);
        return;
    }

    // Check if this alert type already exists
    for (auto& alert : activeAlerts) {
        if (alert.type == alertType) {
            // Update existing alert
            alert.reason = reason;
            alert.timestamp = esphome::millis();
            return;
        }
    }

    // Add new alert
    activeAlerts.push_back(Alert(alertType, reason));
}

void CentralStatusLogger::updateAlertStatus(const char* alertType, const std::string& reason) {
    updateAlertStatus(std::string(alertType), reason);
}

void CentralStatusLogger::clearAlert(const std::string& alertType) {
    activeAlerts.erase(
        std::remove_if(activeAlerts.begin(), activeAlerts.end(),
            [&alertType](const Alert& alert) { return alert.type == alertType; }),
        activeAlerts.end()
    );
}

void CentralStatusLogger::clearAlert(const char* alertType) {
    clearAlert(std::string(alertType));
}

void CentralStatusLogger::clearAllAlerts() {
    activeAlerts.clear();
}

// ============================================================================
// ENHANCED ALERT METHODS (for error handling)
// ============================================================================

void CentralStatusLogger::resolveAlert(const std::string& alertType) {
    for (auto& alert : activeAlerts) {
        if (alert.type == alertType && alert.status == AlertStatus::ACTIVE) {
            alert.status = AlertStatus::RESOLVED;
            alert.resolved_timestamp = esphome::millis();
            ESP_LOGI("CentralStatusLogger", "[RESOLVED] %s (was active for %.1f seconds)",
                     alertType.c_str(), alert.getActiveDurationSeconds());
            return;
        }
    }
}

void CentralStatusLogger::resolveAlert(const char* alertType) {
    resolveAlert(std::string(alertType));
}

void CentralStatusLogger::updateAlertWithContext(
    const std::string& alertType,
    const std::string& reason,
    const std::string& root_cause,
    const std::string& user_action,
    const std::string& operation_context,
    const std::string& recovery_plan,
    uint8_t max_retries)
{
    // Check if alert already exists (update it)
    for (auto& alert : activeAlerts) {
        if (alert.type == alertType && alert.status == AlertStatus::ACTIVE) {
            alert.reason = reason;
            alert.root_cause = root_cause;
            alert.user_action = user_action;
            alert.operation_context = operation_context;
            alert.recovery_plan = recovery_plan;
            alert.max_retries = max_retries;
            return;
        }
    }

    // Create new alert with full context
    Alert newAlert(alertType, reason, root_cause, user_action,
                   operation_context, recovery_plan, max_retries);
    activeAlerts.push_back(newAlert);

    ESP_LOGE("CentralStatusLogger", "ALERT: %s - %s", alertType.c_str(), reason.c_str());
    if (!root_cause.empty()) {
        ESP_LOGI("CentralStatusLogger", "  Root Cause: %s", root_cause.c_str());
    }
    if (!user_action.empty()) {
        ESP_LOGI("CentralStatusLogger", "  Next Steps: %s", user_action.c_str());
    }
    if (!operation_context.empty()) {
        ESP_LOGI("CentralStatusLogger", "  Context: %s", operation_context.c_str());
    }
    if (!recovery_plan.empty()) {
        ESP_LOGI("CentralStatusLogger", "  Recovery: %s", recovery_plan.c_str());
    }
}

void CentralStatusLogger::incrementAlertRetry(const std::string& alertType) {
    for (auto& alert : activeAlerts) {
        if (alert.type == alertType && alert.status == AlertStatus::ACTIVE) {
            alert.retry_count++;
            ESP_LOGI("CentralStatusLogger", "Alert %s retry %u/%u",
                     alertType.c_str(), alert.retry_count, alert.max_retries);
            return;
        }
    }
}

std::vector<Alert> CentralStatusLogger::getActiveAlerts() const {
    std::vector<Alert> active;
    for (const auto& alert : activeAlerts) {
        if (alert.status == AlertStatus::ACTIVE) {
            active.push_back(alert);
        }
    }
    return active;
}

std::vector<Alert> CentralStatusLogger::getResolvedAlerts() const {
    std::vector<Alert> resolved;
    for (const auto& alert : activeAlerts) {
        if (alert.status == AlertStatus::RESOLVED) {
            resolved.push_back(alert);
        }
    }
    return resolved;
}

int CentralStatusLogger::getResolvedAlertCount() const {
    int count = 0;
    for (const auto& alert : activeAlerts) {
        if (alert.status == AlertStatus::RESOLVED) {
            count++;
        }
    }
    return count;
}

void CentralStatusLogger::pruneResolvedAlerts(uint32_t max_age_ms) {
    uint32_t now = esphome::millis();
    activeAlerts.erase(
        std::remove_if(activeAlerts.begin(), activeAlerts.end(),
            [now, max_age_ms](const Alert& alert) {
                if (alert.status == AlertStatus::RESOLVED) {
                    uint32_t age = now - alert.resolved_timestamp;
                    return age > max_age_ms;
                }
                return false;
            }),
        activeAlerts.end());
}

void CentralStatusLogger::updateIP(const std::string& ip) {
    currentIP = ip;
}

void CentralStatusLogger::updateIP(const char* ip) {
    currentIP = std::string(ip);
}

void CentralStatusLogger::updateWebServerStatus(bool online, bool clientConnected) {
    webServerOnline = online;
    webServerClientConnected = clientConnected;
}

void CentralStatusLogger::updateI2CHardwareStatus(const std::vector<I2CDeviceInfo>& devices) {
    i2cDevices = devices;
    i2cScanPerformed = true;
}

void CentralStatusLogger::updateUARTHardwareStatus(const std::vector<UARTDeviceInfo>& devices) {
    uartDevices = devices;
    uartStatusUpdated = true;
}

void CentralStatusLogger::updateOneWireHardwareStatus(const std::vector<OneWireDeviceInfo>& devices) {
    oneWireDevices = devices;
    oneWireStatusUpdated = true;
}

void CentralStatusLogger::updateShellyHardwareStatus(const std::vector<ShellyDeviceInfo>& devices) {
    shellyDevices_ = devices;
    shellyStatusUpdated_ = true;
}

void CentralStatusLogger::updatePumpConfigurations(const std::vector<PumpConfigInfo>& configs) {
    pumpConfigs = configs;
    pumpConfigsUpdated = true;
}

void CentralStatusLogger::updateCalendarStatus(uint8_t currentDay, float phMin, float phMax,
                                               float nutrientA, float nutrientB, float nutrientC,
                                               bool safeMode) {
    calendarCurrentDay = currentDay;
    calendarPhMin = phMin;
    calendarPhMax = phMax;
    calendarNutrientA = nutrientA;
    calendarNutrientB = nutrientB;
    calendarNutrientC = nutrientC;
    calendarSafeMode = safeMode;
    calendarStatusUpdated = true;
}

void CentralStatusLogger::logStatus() {
    // Check if status reports are enabled
    if (!enableReports_) {
        return;
    }

    printSeparator('=');
    ESP_LOGI(TAG, "  PLANTOS STATUS - %s", getFormattedTime().c_str());
    printSeparator('=');

    // 420 easter egg: Print ASCII art at 4:20 AM and PM
    if (mode420_) {
        time_t now = time(NULL);
        struct tm* timeinfo = localtime(&now);

        // Check if time is 4:20 (04:20 or 16:20) and NTP is synchronized
        if (timeinfo->tm_year >= (2020 - 1900) &&
            (timeinfo->tm_hour == 4 || timeinfo->tm_hour == 16) &&
            timeinfo->tm_min == 20) {
            print420Art();
        }
    }

    // CRITICAL: Print alerts immediately after time if any are active
    if (hasActiveAlerts()) {
        printAlertBanner();
    }

    // Network Status
    ESP_LOGI(TAG, "--- NETWORK ---");
    std::string webStatus = webServerOnline ? (webServerClientConnected ? "Online (Client)" : "Online") : "Offline";
    ESP_LOGI(TAG, "  IP: %s | Web: %s", currentIP.c_str(), webStatus.c_str());

    // Hardware Status - flat list of all devices
    ESP_LOGI(TAG, "--- HARDWARE ---");

    // I²C Devices
    if (i2cScanPerformed && !i2cDevices.empty()) {
        for (const auto& device : i2cDevices) {
            if (device.found) {
                ESP_LOGI(TAG, "  \033[32m✓ 0x%02X: %s\033[0m", device.address, device.name.c_str());
            } else if (device.critical) {
                // Special case: EZO pH I2C missing but UART version is present
                bool isEzoPh = (device.address == 0x61 || device.name.find("EZO pH") != std::string::npos);
                bool uartEzoPhPresent = false;
                if (isEzoPh && uartStatusUpdated) {
                    for (const auto& uart : uartDevices) {
                        if (uart.name.find("EZO pH") != std::string::npos && uart.ready) {
                            uartEzoPhPresent = true;
                            break;
                        }
                    }
                }
                if (isEzoPh && uartEzoPhPresent) {
                    ESP_LOGW(TAG, "  \033[33m⚠ 0x%02X: %s (UART active)\033[0m", device.address, device.name.c_str());
                } else {
                    ESP_LOGE(TAG, "  \033[31m✗ 0x%02X: %s (MISSING)\033[0m", device.address, device.name.c_str());
                }
            }
        }
    }

    // UART Devices
    if (uartStatusUpdated && !uartDevices.empty()) {
        for (const auto& device : uartDevices) {
            if (device.ready) {
                ESP_LOGI(TAG, "  \033[32m✓ %s (%s)\033[0m", device.name.c_str(), device.port.c_str());
            } else if (device.critical) {
                ESP_LOGE(TAG, "  \033[31m✗ %s (%s) - NOT READY\033[0m", device.name.c_str(), device.port.c_str());
            }
        }
    }

    // 1-Wire Devices
    if (oneWireStatusUpdated && !oneWireDevices.empty()) {
        for (const auto& device : oneWireDevices) {
            if (device.ready) {
                if (device.status.empty()) {
                    ESP_LOGI(TAG, "  \033[32m✓ %s (%s)\033[0m", device.name.c_str(), device.port.c_str());
                } else {
                    ESP_LOGI(TAG, "  \033[32m✓ %s (%s) - %s\033[0m", device.name.c_str(), device.port.c_str(), device.status.c_str());
                }
            } else if (device.critical) {
                ESP_LOGE(TAG, "  \033[31m✗ %s (%s) - NOT READY\033[0m", device.name.c_str(), device.port.c_str());
            }
        }
    }

    // Shelly HTTP Devices
    if (shellyStatusUpdated_ && !shellyDevices_.empty()) {
        for (const auto& device : shellyDevices_) {
            if (device.reachable) {
                uint32_t hours = device.uptime_seconds / 3600;
                uint32_t mins = (device.uptime_seconds % 3600) / 60;
                ESP_LOGI(TAG, "  \033[32m✓ %s (%s) - up %uh%um\033[0m", device.name.c_str(), device.ip.c_str(), hours, mins);
            } else {
                ESP_LOGE(TAG, "  \033[31m✗ %s (%s) - OFFLINE\033[0m", device.name.c_str(), device.ip.c_str());
            }
        }
    }

    // Water Level Sensors - compact status
    if (waterLevelSensorsAvailable) {
        bool highSensor = waterLevelHighSensor;
        bool lowSensor = waterLevelLowSensor;
        if (highSensor && lowSensor) {
            ESP_LOGI(TAG, "  \033[32m✓ Water Level: FULL (H=ON, L=ON)\033[0m");
        } else if (lowSensor && !highSensor) {
            ESP_LOGI(TAG, "  \033[32m✓ Water Level: NORMAL (H=OFF, L=ON)\033[0m");
        } else if (!highSensor && !lowSensor) {
            ESP_LOGW(TAG, "  \033[33m⚠ Water Level: LOW (H=OFF, L=OFF)\033[0m");
        } else {
            ESP_LOGE(TAG, "  \033[31m✗ Water Level: ERROR (H=ON, L=OFF)\033[0m");
        }
    }

    // Sensor Data
    ESP_LOGI(TAG, "--- SENSORS ---");
    if (filteredPH > 0.0f) {
        ESP_LOGI(TAG, "  pH: %.2f", filteredPH);
    } else {
        ESP_LOGW(TAG, "  pH: N/A");
    }
    if (waterTempAvailable) {
        ESP_LOGI(TAG, "  Water Temp: %.1f°C", waterTemperature);
    } else {
        ESP_LOGW(TAG, "  Water Temp: N/A");
    }

    // Calendar Status
    ESP_LOGI(TAG, "--- CALENDAR ---");
    if (calendarStatusUpdated) {
        ESP_LOGI(TAG, "  Day: %d/120 | pH: %.1f-%.1f | Auto: %s",
                 calendarCurrentDay, calendarPhMin, calendarPhMax,
                 calendarSafeMode ? "OFF" : "ON");
        ESP_LOGI(TAG, "  Nutrients (mL/L): A=%.2f; B=%.2f; C=%.2f",
                 calendarNutrientA, calendarNutrientB, calendarNutrientC);
    } else {
        ESP_LOGW(TAG, "  Calendar not configured");
    }

    // Pump Configurations
    // NOTE: Commented out - not needed right now but might be reactivated or repurposed later
    // ESP_LOGI(TAG, "--- PUMP CONFIGURATION ---");
    // if (pumpConfigsUpdated && !pumpConfigs.empty()) {
    //     for (const auto& pump : pumpConfigs) {
    //         ESP_LOGI(TAG, "  %s:", pump.pump_name.c_str());
    //         ESP_LOGI(TAG, "    Flow Rate:    %.3f mL/s", pump.flow_rate_ml_s);
    //         ESP_LOGI(TAG, "    PWM Intensity: %.0f%%", pump.pwm_intensity * 100.0f);
    //     }
    // } else {
    //     ESP_LOGW(TAG, "  Pump configurations not yet loaded");
    // }
    //
    // ESP_LOGI(TAG, "");

    // System State - Unified controller architecture
    ESP_LOGI(TAG, "--- STATE ---");
    ESP_LOGI(TAG, "  Controller: %s | Maintenance: %s",
             controllerState.c_str(), maintenanceMode ? "ON" : "OFF");
    if (!psmEventID.empty() && psmEventAge >= 0) {
        ESP_LOGI(TAG, "  PSM: %s (Status: %d, Age: %lld sec)",
                 psmEventID.c_str(), psmEventStatus, (long long)psmEventAge);
    }

    // Active Alerts (listed under system state)
    std::vector<Alert> active = getActiveAlerts();
    if (!active.empty()) {
        ESP_LOGI(TAG, "  Active Alerts: %zu", active.size());
        for (size_t i = 0; i < active.size(); i++) {
            const Alert& alert = active[i];
            ESP_LOGE(TAG, "    \033[31m[%zu] %s: %s (%.0fs)\033[0m",
                     i + 1, alert.type.c_str(), alert.reason.c_str(),
                     alert.getActiveDurationSeconds());
            if (!alert.user_action.empty()) {
                ESP_LOGE(TAG, "        Action: %s", alert.user_action.c_str());
            }
        }
    } else {
        ESP_LOGI(TAG, "  Alerts: ALL CLEAR");
    }
    printSeparator('=');

    // Update last log timestamp
    lastLogTimestamp = esphome::millis();
}

bool CentralStatusLogger::hasActiveAlerts() const {
    return !activeAlerts.empty();
}

int CentralStatusLogger::getAlertCount() const {
    return activeAlerts.size();
}

std::string CentralStatusLogger::getFormattedTime() {
    time_t now = time(NULL);
    struct tm* timeinfo = localtime(&now);

    if (timeinfo->tm_year < (2020 - 1900)) {
        // NTP not synchronized yet
        return "NTP Not Synchronized";
    }

    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    return std::string(buffer);
}

void CentralStatusLogger::printAlertBanner() {
    ESP_LOGW(TAG, "");
    printSeparator('*');
    printSeparator('*');
    ESP_LOGW(TAG, "***                     CRITICAL ALERTS ACTIVE                       ***");
    printSeparator('*');
    printSeparator('*');

    // Get only active alerts (filter out resolved)
    std::vector<Alert> active = getActiveAlerts();

    for (size_t i = 0; i < active.size(); i++) {
        const Alert& alert = active[i];

        ESP_LOGW(TAG, "");
        ESP_LOGW(TAG, "*** ALERT #%zu: %s", i + 1, alert.type.c_str());
        ESP_LOGW(TAG, "*** Brief: %s", alert.reason.c_str());
        ESP_LOGW(TAG, "*** Active for: %.1f seconds", alert.getActiveDurationSeconds());

        // Display 4-part error context if available
        if (!alert.root_cause.empty()) {
            ESP_LOGW(TAG, "***");
            ESP_LOGW(TAG, "*** Root Cause: %s", alert.root_cause.c_str());
        }
        if (!alert.user_action.empty()) {
            ESP_LOGW(TAG, "*** User Action: %s", alert.user_action.c_str());
        }
        if (!alert.operation_context.empty()) {
            ESP_LOGW(TAG, "*** Context: %s", alert.operation_context.c_str());
        }
        if (!alert.recovery_plan.empty()) {
            ESP_LOGW(TAG, "*** Recovery: %s", alert.recovery_plan.c_str());
        }

        // Display retry information if applicable
        if (alert.max_retries > 0) {
            ESP_LOGW(TAG, "*** Retry: %u / %u attempts", alert.retry_count, alert.max_retries);
        }
    }

    ESP_LOGW(TAG, "");
    printSeparator('*');
    printSeparator('*');
}

void CentralStatusLogger::printSeparator(char c, int length) {
    char buffer[81];
    for (int i = 0; i < length && i < 80; i++) {
        buffer[i] = c;
    }
    buffer[length < 80 ? length : 80] = '\0';
    ESP_LOGI(TAG, "%s", buffer);
}

void CentralStatusLogger::set420Mode(bool enabled) {
    mode420_ = enabled;
}

void CentralStatusLogger::print420Art() {
    ESP_LOGI(TAG, "                                    =%%         ");
    ESP_LOGI(TAG, "============================  @@   @=%%@   @@   ");
    ESP_LOGI(TAG, " ##   ##  #######    #####     +@@ @+%%@ @=%%    ");
    ESP_LOGI(TAG, "##   ##  ##    ##  ##   ##     @=#%%.=%% =+@    ");
    ESP_LOGI(TAG, "#######      ###   ##   ##       =%#=@==%%      ");
    ESP_LOGI(TAG, "     ##    ###     ##   ##    @+++%%=%%#%=%%@   ");
    ESP_LOGI(TAG, "     ##  ########   #####        @#%=@%%@      ");
    ESP_LOGI(TAG, "============================        +          ");
}

void CentralStatusLogger::logWaterLevelStatus(bool high_sensor, bool low_sensor, bool empty_sensor, bool sensors_available) {
    ESP_LOGI(TAG, "--- WATER LEVEL STATUS ---");

    if (!sensors_available) {
        ESP_LOGI(TAG, "  Status: Using time-based fill/drain limits (no sensors)");
        return;
    }

    // ========================================================================
    // 5-STATE WATER LEVEL SYSTEM
    // ========================================================================
    std::string level_status;

    // STATE 1: FULL (HIGH=ON, LOW=ON)
    if (high_sensor && low_sensor) {
        level_status = "FULL (above HIGH sensor)";
        ESP_LOGI(TAG, "  ┌─────────┐");
        ESP_LOGI(TAG, "  │█████████│ ← HIGH");
        ESP_LOGI(TAG, "  │█████████│");
        ESP_LOGI(TAG, "  │█████████│ ← LOW");
        ESP_LOGI(TAG, "  │█████████│");
        ESP_LOGI(TAG, "  │█████████│ ← EMPTY");
        ESP_LOGI(TAG, "  └─────────┘");
    }
    // STATE 2: NORMAL (HIGH=OFF, LOW=ON)
    else if (!high_sensor && low_sensor) {
        level_status = "NORMAL (between HIGH and LOW)";
        ESP_LOGI(TAG, "  ┌─────────┐");
        ESP_LOGI(TAG, "  │         │ ← HIGH");
        ESP_LOGI(TAG, "  │█████████│");
        ESP_LOGI(TAG, "  │█████████│ ← LOW");
        ESP_LOGI(TAG, "  │█████████│");
        ESP_LOGI(TAG, "  │█████████│ ← EMPTY");
        ESP_LOGI(TAG, "  └─────────┘");
    }
    // STATE 3: LOW (HIGH=OFF, LOW=OFF, EMPTY=ON) - AUTO-FEED TRIGGER ZONE
    else if (!high_sensor && !low_sensor && empty_sensor) {
        level_status = "LOW (between LOW and EMPTY) ⚡ AUTO-FEED ZONE";
        ESP_LOGI(TAG, "  ┌─────────┐");
        ESP_LOGI(TAG, "  │         │ ← HIGH");
        ESP_LOGI(TAG, "  │         │");
        ESP_LOGI(TAG, "  │         │ ← LOW");
        ESP_LOGI(TAG, "  │▒▒▒▒▒▒▒▒▒│ ⚡ AUTO-FEED");
        ESP_LOGI(TAG, "  │▒▒▒▒▒▒▒▒▒│ ← EMPTY");
        ESP_LOGI(TAG, "  └─────────┘");
        ESP_LOGW(TAG, "  ⚡ AUTO-FEEDING TRIGGER ZONE");
        ESP_LOGI(TAG, "     (will trigger if enabled & not fed today)");
    }
    // STATE 4: EMPTY (HIGH=OFF, LOW=OFF, EMPTY=OFF) - DANGER ZONE
    else if (!high_sensor && !low_sensor && !empty_sensor) {
        level_status = "EMPTY (below EMPTY sensor) ⚠️ DANGER";
        ESP_LOGI(TAG, "  ┌─────────┐");
        ESP_LOGI(TAG, "  │         │ ← HIGH");
        ESP_LOGI(TAG, "  │         │");
        ESP_LOGI(TAG, "  │         │ ← LOW");
        ESP_LOGI(TAG, "  │         │");
        ESP_LOGI(TAG, "  │         │ ← EMPTY");
        ESP_LOGI(TAG, "  └─────────┘");
        ESP_LOGE(TAG, "  ⚠️ Danger - Tank below Empty");
    }
    // STATE 5: ERROR (HIGH=ON, LOW=OFF) - INVALID STATE
    else {
        level_status = "ERROR (invalid sensor state)";
        ESP_LOGE(TAG, "  ┌─────────┐");
        ESP_LOGE(TAG, "  │ !ERROR! │");
        ESP_LOGE(TAG, "  │  CHECK  │");
        ESP_LOGE(TAG, "  │  WIRING │");
        ESP_LOGE(TAG, "  │ INVALID │");
        ESP_LOGE(TAG, "  │  STATE  │");
        ESP_LOGE(TAG, "  └─────────┘");
        ESP_LOGE(TAG, "  ALERT: Invalid sensor combination detected");
        ESP_LOGE(TAG, "  Possible causes: Loose wiring, sensor failure, debris");
    }

    ESP_LOGI(TAG, "  Water Level: %s", level_status.c_str());
    ESP_LOGI(TAG, "  Sensors: HIGH=%s, LOW=%s, EMPTY=%s",
             high_sensor ? "ON" : "OFF",
             low_sensor ? "ON" : "OFF",
             empty_sensor ? "ON" : "OFF");
}

void CentralStatusLogger::configure(bool enableReports, uint32_t reportIntervalMs, bool verboseMode) {
    enableReports_ = enableReports;
    reportInterval_ = reportIntervalMs;
    verboseMode_ = verboseMode;

    ESP_LOGI(TAG, "Configuration updated:");
    ESP_LOGI(TAG, "  Status Reports: %s", enableReports_ ? "ENABLED" : "DISABLED");
    if (enableReports_) {
        ESP_LOGI(TAG, "  Report Interval: %u ms", reportInterval_);
    }
    ESP_LOGI(TAG, "  Verbose Mode: %s", verboseMode_ ? "ENABLED" : "DISABLED");
}

bool CentralStatusLogger::shouldPrintStatusReport() {
    if (!enableReports_) {
        return false;
    }

    uint32_t now = esphome::millis();
    if (now - lastLogTimestamp >= reportInterval_) {
        return true;
    }

    return false;
}
