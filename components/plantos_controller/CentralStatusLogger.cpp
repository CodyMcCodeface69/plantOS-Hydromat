#include "CentralStatusLogger.h"

static const char *TAG = "status.logger";

CentralStatusLogger::CentralStatusLogger()
    : currentIP("0.0.0.0"),
      lastLogTimestamp(0),
      filteredPH(7.0),
      activeRoutine("INIT"),
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

void CentralStatusLogger::logStatus() {
    // Check if status reports are enabled
    if (!enableReports_) {
        return;
    }

    printSeparator('=');
    ESP_LOGI(TAG, "  PLANTOS SYSTEM STATUS REPORT");
    printSeparator('=');

    // System Time
    ESP_LOGI(TAG, "System Time: %s", getFormattedTime().c_str());

    // 420 easter egg: Print ASCII art at 4:20 AM and PM
    if (mode420_) {
        time_t now = time(NULL);
        struct tm* timeinfo = localtime(&now);

        // Check if time is 4:20 (04:20 or 16:20) and NTP is synchronized
        if (timeinfo->tm_year >= (2020 - 1900) &&
            (timeinfo->tm_hour == 4 || timeinfo->tm_hour == 16) &&
            timeinfo->tm_min == 20) {
            ESP_LOGI(TAG, "");
            print420Art();
            ESP_LOGI(TAG, "");
        }
    }

    // CRITICAL: Print alerts immediately after time if any are active
    if (hasActiveAlerts()) {
        printAlertBanner();
    }

    ESP_LOGI(TAG, "");

    // Network Status
    ESP_LOGI(TAG, "--- NETWORK STATUS ---");
    ESP_LOGI(TAG, "  IP Address: %s", currentIP.c_str());

    if (webServerOnline) {
        if (webServerClientConnected) {
            ESP_LOGI(TAG, "  Web Server: ONLINE (Client Connected)");
        } else {
            ESP_LOGI(TAG, "  Web Server: ONLINE (No Clients)");
        }
    } else {
        ESP_LOGI(TAG, "  Web Server: OFFLINE");
    }

    ESP_LOGI(TAG, "");

    // Hardware Status (I²C and UART Devices)
    ESP_LOGI(TAG, "--- HARDWARE STATUS ---");

    // I²C Devices
    if (i2cScanPerformed) {
        ESP_LOGI(TAG, "  I²C Devices:");
        if (i2cDevices.empty()) {
            ESP_LOGW(TAG, "    No I²C devices found");
        } else {
            // Separate devices into found and missing critical
            std::vector<I2CDeviceInfo> foundDevices;
            std::vector<I2CDeviceInfo> missingCritical;

            for (const auto& device : i2cDevices) {
                if (device.found) {
                    foundDevices.push_back(device);
                } else if (device.critical) {
                    missingCritical.push_back(device);
                }
            }

            // Display found devices in green
            if (!foundDevices.empty()) {
                for (const auto& device : foundDevices) {
                    // ANSI Green: \033[32m, Reset: \033[0m
                    ESP_LOGI(TAG, "    \033[32m✓ 0x%02X: %s\033[0m", device.address, device.name.c_str());
                }
            }

            // Display missing critical devices in red
            if (!missingCritical.empty()) {
                for (const auto& device : missingCritical) {
                    // ANSI Red: \033[31m, Reset: \033[0m
                    ESP_LOGE(TAG, "    \033[31m✗ 0x%02X: %s (MISSING)\033[0m", device.address, device.name.c_str());
                }
            }

            ESP_LOGI(TAG, "    Total: %d found", foundDevices.size());
        }
    } else {
        ESP_LOGI(TAG, "  I²C: Scan not yet performed");
    }

    // UART Devices
    if (uartStatusUpdated) {
        ESP_LOGI(TAG, "  UART Devices:");
        if (uartDevices.empty()) {
            ESP_LOGW(TAG, "    No UART devices configured");
        } else {
            // Separate devices into ready and not ready
            std::vector<UARTDeviceInfo> readyDevices;
            std::vector<UARTDeviceInfo> notReadyCritical;

            for (const auto& device : uartDevices) {
                if (device.ready) {
                    readyDevices.push_back(device);
                } else if (device.critical) {
                    notReadyCritical.push_back(device);
                }
            }

            // Display ready devices in green
            if (!readyDevices.empty()) {
                for (const auto& device : readyDevices) {
                    // ANSI Green: \033[32m, Reset: \033[0m
                    if (device.status.empty()) {
                        ESP_LOGI(TAG, "    \033[32m✓ %s (%s)\033[0m", device.name.c_str(), device.port.c_str());
                    } else {
                        ESP_LOGI(TAG, "    \033[32m✓ %s (%s) - %s\033[0m", device.name.c_str(), device.port.c_str(), device.status.c_str());
                    }
                }
            }

            // Display not ready critical devices in red
            if (!notReadyCritical.empty()) {
                for (const auto& device : notReadyCritical) {
                    // ANSI Red: \033[31m, Reset: \033[0m
                    if (device.status.empty()) {
                        ESP_LOGE(TAG, "    \033[31m✗ %s (%s) - NOT READY\033[0m", device.name.c_str(), device.port.c_str());
                    } else {
                        ESP_LOGE(TAG, "    \033[31m✗ %s (%s) - %s\033[0m", device.name.c_str(), device.port.c_str(), device.status.c_str());
                    }
                }
            }

            ESP_LOGI(TAG, "    Total: %d ready", readyDevices.size());
        }
    } else {
        ESP_LOGI(TAG, "  UART: Status not yet updated");
    }

    ESP_LOGI(TAG, "");

    // Sensor Data
    ESP_LOGI(TAG, "--- SENSOR DATA ---");
    if (filteredPH > 0.0f) {
        ESP_LOGI(TAG, "  pH: %.2f", filteredPH);
    } else {
        ESP_LOGW(TAG, "  pH: No reading available");
    }

    ESP_LOGI(TAG, "");

    // System State - Unified controller architecture
    ESP_LOGI(TAG, "--- SYSTEM STATE ---");

    ESP_LOGI(TAG, "  Controller State:   %s", controllerState.c_str());

    ESP_LOGI(TAG, "  Maintenance Mode:   %s", maintenanceMode ? "ENABLED" : "DISABLED");

    if (!psmEventID.empty() && psmEventAge >= 0) {
        ESP_LOGI(TAG, "  PSM Event:          %s (Status: %d, Age: %lld sec)",
                 psmEventID.c_str(), psmEventStatus, (long long)psmEventAge);
    } else {
        ESP_LOGI(TAG, "  PSM Event:          None");
    }

    ESP_LOGI(TAG, "");

    // Alert Summary
    ESP_LOGI(TAG, "--- ALERT STATUS ---");
    if (hasActiveAlerts()) {
        ESP_LOGI(TAG, "  Active Alerts: %d", getAlertCount());
    } else {
        ESP_LOGI(TAG, "  Status: ALL CLEAR");
    }

    printSeparator('=');
    ESP_LOGI(TAG, "");

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

    for (size_t i = 0; i < activeAlerts.size(); i++) {
        const Alert& alert = activeAlerts[i];

        ESP_LOGW(TAG, "");
        ESP_LOGW(TAG, "*** ALERT #%zu: %s", i + 1, alert.type.c_str());
        ESP_LOGW(TAG, "*** Reason: %s", alert.reason.c_str());

        uint32_t alertAge = (esphome::millis() - alert.timestamp) / 1000;
        ESP_LOGW(TAG, "*** Active for: %u seconds", alertAge);
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
