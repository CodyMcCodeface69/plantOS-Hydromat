/**
 * Example Usage of CentralStatusLogger
 *
 * This example demonstrates:
 * - Initialization of the logger
 * - Periodic 30-second status logging
 * - Updating system values
 * - Setting multiple simultaneous alerts
 * - Clearing individual and all alerts
 */

#include <Arduino.h>
#include "CentralStatusLogger.h"

// Create global logger instance
CentralStatusLogger statusLogger;

// Timing variables for 30-second periodic logging
unsigned long lastStatusLog = 0;
const unsigned long STATUS_LOG_INTERVAL = 30000; // 30 seconds

// Simulation variables
float simulatedPH = 7.0;
int loopCounter = 0;

void setup() {
    Serial.begin(115200);
    delay(1000); // Wait for serial to stabilize

    Serial.println("\n\n=== PlantOS CentralStatusLogger Example ===\n");

    // Initialize the logger
    statusLogger.begin();

    // Simulate initial system configuration
    statusLogger.updateIP("192.168.1.100");
    statusLogger.updateWebServerStatus(true, false);
    statusLogger.updateStatus(7.0, "IDLE");

    // Print initial status immediately
    statusLogger.logStatus();
    lastStatusLog = millis();
}

void loop() {
    // Simulate system activity and various scenarios
    loopCounter++;

    // Simulate pH drift over time
    simulatedPH = 7.0 + (sin(millis() / 10000.0) * 2.0);

    // Update system status periodically
    if (loopCounter % 100 == 0) {
        String routine = "IDLE";

        // Simulate routine changes
        if (simulatedPH < 5.5) {
            routine = "DOSING_BASE";
        } else if (simulatedPH > 8.5) {
            routine = "DOSING_ACID";
        }

        statusLogger.updateStatus(simulatedPH, routine);
    }

    // SCENARIO SIMULATION: Demonstrate alert functionality
    unsigned long currentTime = millis();

    // After 10 seconds: Trigger a water spill alert
    if (currentTime > 10000 && currentTime < 11000) {
        statusLogger.updateAlertStatus("SPILL", "Water detected on floor sensor #2");
    }

    // After 20 seconds: Add pH critical alert (now 2 simultaneous alerts)
    if (currentTime > 20000 && currentTime < 21000) {
        statusLogger.updateAlertStatus("PH_CRITICAL", "pH level outside safe range: " + String(simulatedPH, 2));
    }

    // After 35 seconds: Add temperature alert (now 3 simultaneous alerts)
    if (currentTime > 35000 && currentTime < 36000) {
        statusLogger.updateAlertStatus("TEMPERATURE", "Reservoir temperature too high: 32.5°C");
    }

    // After 50 seconds: Clear the spill alert (2 alerts remain)
    if (currentTime > 50000 && currentTime < 51000) {
        statusLogger.clearAlert("SPILL");
    }

    // After 70 seconds: Clear all remaining alerts
    if (currentTime > 70000 && currentTime < 71000) {
        statusLogger.clearAllAlerts();
    }

    // After 80 seconds: Simulate web client connection
    if (currentTime > 80000 && currentTime < 81000) {
        statusLogger.updateWebServerStatus(true, true);
    }

    // After 95 seconds: Simulate web client disconnection
    if (currentTime > 95000 && currentTime < 96000) {
        statusLogger.updateWebServerStatus(true, false);
    }

    // PERIODIC 30-SECOND STATUS LOGGING (non-blocking)
    if (currentTime - lastStatusLog >= STATUS_LOG_INTERVAL) {
        statusLogger.logStatus();
        lastStatusLog = currentTime;
    }

    // Small delay to prevent overwhelming the serial output
    delay(100);
}
