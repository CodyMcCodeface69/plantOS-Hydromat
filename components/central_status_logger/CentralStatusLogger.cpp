#include "CentralStatusLogger.h"

CentralStatusLogger::CentralStatusLogger()
    : currentIP("0.0.0.0"),
      lastLogTimestamp(0),
      filteredPH(7.0),
      activeRoutine("INIT"),
      webServerOnline(false),
      webServerClientConnected(false),
      controllerState("UNKNOWN"),
      logicState("UNKNOWN"),
      maintenanceMode(false),
      psmEventID(""),
      psmEventStatus(0),
      psmEventAge(-1) {
}

void CentralStatusLogger::begin() {
    Serial.println("\n[CentralStatusLogger] Initializing...");

    // Set initial values
    currentIP = "0.0.0.0";
    lastLogTimestamp = millis();
    filteredPH = 7.0;
    activeRoutine = "IDLE";
    webServerOnline = false;
    webServerClientConnected = false;
    controllerState = "UNKNOWN";
    logicState = "UNKNOWN";
    maintenanceMode = false;
    psmEventID = "";
    psmEventStatus = 0;
    psmEventAge = -1;
    activeAlerts.clear();

    printSeparator();
    Serial.println("  CENTRAL STATUS LOGGER INITIALIZED");
    printSeparator();
    Serial.println();
}

void CentralStatusLogger::updateStatus(float ph, const String& routine) {
    filteredPH = ph;
    activeRoutine = routine;
}

void CentralStatusLogger::updateControllerState(const String& state) {
    controllerState = state;
}

void CentralStatusLogger::updateLogicState(const String& state) {
    logicState = state;
}

void CentralStatusLogger::updateMaintenanceMode(bool enabled) {
    maintenanceMode = enabled;
}

void CentralStatusLogger::updatePSMEvent(const String& eventID, int status, int64_t ageSeconds) {
    psmEventID = eventID;
    psmEventStatus = status;
    psmEventAge = ageSeconds;
}

void CentralStatusLogger::updateAlertStatus(const String& alertType, const String& reason) {
    if (reason.isEmpty()) {
        // Empty reason means clear this alert
        clearAlert(alertType);
        return;
    }

    // Check if this alert type already exists
    for (auto& alert : activeAlerts) {
        if (alert.type == alertType) {
            // Update existing alert
            alert.reason = reason;
            alert.timestamp = millis();
            return;
        }
    }

    // Add new alert
    activeAlerts.push_back(Alert(alertType, reason));
}

void CentralStatusLogger::clearAlert(const String& alertType) {
    activeAlerts.erase(
        std::remove_if(activeAlerts.begin(), activeAlerts.end(),
            [&alertType](const Alert& alert) { return alert.type == alertType; }),
        activeAlerts.end()
    );
}

void CentralStatusLogger::clearAllAlerts() {
    activeAlerts.clear();
}

void CentralStatusLogger::updateIP(const String& ip) {
    currentIP = ip;
}

void CentralStatusLogger::updateWebServerStatus(bool online, bool clientConnected) {
    webServerOnline = online;
    webServerClientConnected = clientConnected;
}

void CentralStatusLogger::logStatus() {
    printSeparator('=');
    Serial.println("  PLANTOS SYSTEM STATUS REPORT");
    printSeparator('=');

    // System Time
    Serial.print("System Time: ");
    Serial.println(getFormattedTime());

    // CRITICAL: Print alerts immediately after time if any are active
    if (hasActiveAlerts()) {
        printAlertBanner();
    }

    Serial.println();

    // Network Status
    Serial.println("--- NETWORK STATUS ---");
    Serial.print("  IP Address: ");
    Serial.println(currentIP);

    Serial.print("  Web Server: ");
    if (webServerOnline) {
        Serial.print("ONLINE");
        if (webServerClientConnected) {
            Serial.println(" (Client Connected)");
        } else {
            Serial.println(" (No Clients)");
        }
    } else {
        Serial.println("OFFLINE");
    }

    Serial.println();

    // Sensor Data
    Serial.println("--- SENSOR DATA ---");
    Serial.print("  Filtered pH: ");
    Serial.print(filteredPH, 2);
    Serial.println();

    Serial.println();

    // System State - Now shows ALL state machines
    Serial.println("--- SYSTEM STATE ---");

    Serial.print("  Controller FSM:     ");
    Serial.println(controllerState);

    Serial.print("  PlantOS Logic FSM:  ");
    Serial.println(logicState);

    Serial.print("  Maintenance Mode:   ");
    Serial.println(maintenanceMode ? "ENABLED" : "DISABLED");

    Serial.print("  Active Routine:     ");
    Serial.println(activeRoutine);

    if (!psmEventID.isEmpty() && psmEventAge >= 0) {
        Serial.print("  PSM Event:          ");
        Serial.print(psmEventID);
        Serial.print(" (Status: ");
        Serial.print(psmEventStatus);
        Serial.print(", Age: ");
        Serial.print((long)psmEventAge);
        Serial.println(" sec)");
    } else {
        Serial.println("  PSM Event:          None");
    }

    Serial.println();

    // Alert Summary
    Serial.println("--- ALERT STATUS ---");
    if (hasActiveAlerts()) {
        Serial.print("  Active Alerts: ");
        Serial.println(getAlertCount());
    } else {
        Serial.println("  Status: ALL CLEAR");
    }

    printSeparator('=');
    Serial.println();

    // Update last log timestamp
    lastLogTimestamp = millis();
}

bool CentralStatusLogger::hasActiveAlerts() const {
    return !activeAlerts.empty();
}

int CentralStatusLogger::getAlertCount() const {
    return activeAlerts.size();
}

String CentralStatusLogger::getFormattedTime() {
    time_t now = time(NULL);
    struct tm* timeinfo = localtime(&now);

    if (timeinfo->tm_year < (2020 - 1900)) {
        // NTP not synchronized yet
        return "NTP Not Synchronized";
    }

    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    return String(buffer);
}

void CentralStatusLogger::printAlertBanner() {
    Serial.println();
    printSeparator('*');
    printSeparator('*');
    Serial.println("***                     CRITICAL ALERTS ACTIVE                       ***");
    printSeparator('*');
    printSeparator('*');

    for (size_t i = 0; i < activeAlerts.size(); i++) {
        const Alert& alert = activeAlerts[i];

        Serial.println();
        Serial.print("*** ALERT #");
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.println(alert.type);
        Serial.print("*** Reason: ");
        Serial.println(alert.reason);

        unsigned long alertAge = (millis() - alert.timestamp) / 1000;
        Serial.print("*** Active for: ");
        Serial.print(alertAge);
        Serial.println(" seconds");
    }

    Serial.println();
    printSeparator('*');
    printSeparator('*');
}

void CentralStatusLogger::printSeparator(char c, int length) {
    for (int i = 0; i < length; i++) {
        Serial.print(c);
    }
    Serial.println();
}
