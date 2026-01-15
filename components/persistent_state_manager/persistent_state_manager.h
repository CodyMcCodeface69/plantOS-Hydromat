#pragma once

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cstring>
#include <ctime>

namespace esphome {
namespace persistent_state_manager {

/**
 * CriticalEventLog - Structure representing a critical event that needs persistence
 *
 * This structure is saved to NVS (Non-Volatile Storage) to track critical system
 * events that must survive power loss or unexpected reboots.
 *
 * CRITICAL RISC-V ALIGNMENT FIX:
 * Members are ordered from largest to smallest alignment requirements to ensure
 * proper memory alignment on ESP32-C6 (RISC-V). RISC-V requires strict alignment:
 * - int64_t must be 8-byte aligned
 * - int32_t must be 4-byte aligned
 * - char[] has no alignment requirement
 *
 * OLD ORDER (CRASHES on RISC-V):
 *   char eventID[32]      @ offset 0
 *   int64_t timestampSec  @ offset 32 (NOT 8-byte aligned! 32 % 8 = 0 BUT compiler pads it)
 *   int32_t status        @ offset 40
 *
 * NEW ORDER (FIXED):
 *   int64_t timestampSec  @ offset 0 (8-byte aligned)
 *   int32_t status        @ offset 8 (4-byte aligned)
 *   char eventID[32]      @ offset 12 (no requirement)
 *
 * The __attribute__((aligned(8))) ensures the struct itself is allocated on
 * 8-byte boundaries, preventing misalignment when stored in NVS or passed to
 * FreeRTOS queue operations.
 */
struct __attribute__((aligned(8))) CriticalEventLog {
    int64_t timestampSec;   // Unix timestamp when event was logged (8-byte aligned)
    int32_t status;         // Event status: 0=STARTED, 1=COMPLETED, 2=ERROR, etc.
    char eventID[32];       // Unique event identifier (e.g., "DOSING_ACID", "WATERING_ZONE1")

    CriticalEventLog() {
        timestampSec = 0;
        status = 0;
        memset(eventID, 0, sizeof(eventID));
    }

    CriticalEventLog(const char* id, int64_t ts, int32_t st)
        : timestampSec(ts), status(st) {
        strncpy(eventID, id, sizeof(eventID) - 1);
        eventID[sizeof(eventID) - 1] = '\0';  // Ensure null termination
    }
};

/**
 * PersistentStateManager - Manages critical event logging with NVS persistence
 *
 * ============================================================================
 * PURPOSE
 * ============================================================================
 *
 * This component provides persistent state management for critical system events
 * that must survive power loss, crashes, or unexpected reboots. It's essential
 * for operations where interruption could cause:
 * - Chemical spills (interrupted pump operation)
 * - Water overflow (interrupted valve closure)
 * - Incomplete dosing cycles
 * - Lost calibration data
 *
 * ============================================================================
 * KEY FEATURES
 * ============================================================================
 *
 * 1. NVS PERSISTENCE: Uses ESP32's Non-Volatile Storage to save event logs
 *    that survive power cycles.
 *
 * 2. RECOVERY DETECTION: On boot, checks if previous critical operation was
 *    interrupted and needs recovery action.
 *
 * 3. TIME-BASED VALIDATION: Can detect stale events that are too old to be
 *    relevant (e.g., event from last week vs. event from 5 seconds ago).
 *
 * 4. ATOMIC OPERATIONS: All NVS writes are atomic - either complete or not
 *    written at all.
 *
 * ============================================================================
 * USAGE EXAMPLE
 * ============================================================================
 *
 * PersistentStateManager psm;
 *
 * // On boot - check for interrupted operations
 * void setup() {
 *     psm.begin();
 *
 *     // Check if acid dosing was interrupted (within last 60 seconds)
 *     if (psm.wasInterrupted(60)) {
 *         CriticalEventLog event = psm.getLastEvent();
 *         if (strcmp(event.eventID, "DOSING_ACID") == 0) {
 *             ESP_LOGW(TAG, "RECOVERY: Acid dosing was interrupted!");
 *             // Take recovery action (turn off pump, etc.)
 *         }
 *     }
 * }
 *
 * // Before starting critical operation
 * void startAcidDosing() {
 *     psm.logEvent("DOSING_ACID", 0);  // Log STARTED
 *     turnOnAcidPump();
 * }
 *
 * // After completing critical operation
 * void finishAcidDosing() {
 *     turnOffAcidPump();
 *     psm.clearEvent();  // Clear - operation completed successfully
 * }
 *
 * ============================================================================
 * STATUS CODES
 * ============================================================================
 *
 * Standard status codes (you can define custom ones):
 * 0 = STARTED    - Operation began but not completed
 * 1 = COMPLETED  - Operation finished (though typically you'd clearEvent())
 * 2 = ERROR      - Operation encountered error
 * 3 = PAUSED     - Operation temporarily paused
 * 4 = CANCELLED  - Operation was cancelled
 */
class PersistentStateManager : public Component {
public:
    PersistentStateManager();

    /**
     * ESPHome Component setup() - Initialize NVS and load last saved state
     *
     * Called automatically by ESPHome during component initialization.
     * Attempts to load the last critical event log from NVS.
     */
    void setup() override;

    /**
     * Log a critical event to NVS
     *
     * @param id Event identifier (max 31 chars, e.g., "DOSING_ACID")
     * @param status Event status (0=STARTED, 1=COMPLETED, 2=ERROR, etc.)
     *
     * Updates timestamp to current NTP time and immediately writes to NVS.
     * This is an atomic operation - either fully saved or not saved at all.
     *
     * IMPORTANT: Call this BEFORE starting the critical operation, not after.
     * This ensures the log exists even if power is lost during the operation.
     *
     * Example:
     *     psm.logEvent("DOSING_ACID", 0);  // Log start
     *     turnOnAcidPump();                 // Start operation
     */
    void logEvent(const char* id, int status);

    /**
     * Clear the current critical event log from NVS
     *
     * Call this after successfully completing a critical operation to indicate
     * that no recovery is needed on next boot.
     *
     * Example:
     *     turnOffAcidPump();   // Complete operation
     *     psm.clearEvent();    // Clear - no recovery needed
     */
    void clearEvent();

    /**
     * Check if a critical operation was interrupted
     *
     * @param maxAgeSeconds Maximum age of event to consider (seconds)
     * @return true if event exists AND is within maxAgeSeconds
     *
     * This is the primary recovery detection method. On boot, check if:
     * 1. An event log exists (operation was started)
     * 2. Event timestamp is recent enough to matter
     *
     * Example:
     *     if (psm.wasInterrupted(60)) {  // Within last 60 seconds
     *         ESP_LOGW(TAG, "Operation was interrupted - taking recovery action");
     *         // Check event details and recover
     *     }
     */
    bool wasInterrupted(int64_t maxAgeSeconds);

    /**
     * Get the last logged event
     *
     * @return Copy of the last critical event log
     *
     * Use this to inspect event details for recovery decisions.
     *
     * Example:
     *     CriticalEventLog event = psm.getLastEvent();
     *     ESP_LOGI(TAG, "Last event: %s (status: %d)",
     *              event.eventID, event.status);
     */
    CriticalEventLog getLastEvent() const {
        return current_event_;
    }

    /**
     * Check if any event is currently logged
     *
     * @return true if an event exists in NVS
     */
    bool hasEvent() const {
        return event_exists_;
    }

    /**
     * Get event age in seconds
     *
     * @return Age of current event in seconds, or -1 if no event
     */
    int64_t getEventAge() const;

    // ========================================================================
    // PERSISTENT STATE STORAGE METHODS
    // ========================================================================

    /**
     * Save a boolean state to NVS with a given key
     *
     * @param key NVS key for the state (max 15 chars for ESP32 NVS)
     * @param value Boolean value to save
     * @return true if save was successful
     *
     * Stores a persistent boolean value that survives reboots.
     * Used for user preferences and toggleable states.
     *
     * Example:
     *     psm.saveState("ShutdownState", true);  // Save maintenance mode ON
     */
    bool saveState(const char* key, bool value);

    /**
     * Load a boolean state from NVS with a given key
     *
     * @param key NVS key for the state
     * @param default_value Value to return if key doesn't exist (default: false)
     * @return Loaded boolean value, or default_value if not found
     *
     * Retrieves a persistent boolean value from NVS.
     *
     * Example:
     *     bool maintenance = psm.loadState("ShutdownState");  // Returns false if not set
     */
    bool loadState(const char* key, bool default_value = false);

private:
    // NVS preference handle for persistent storage
    ESPPreferenceObject pref_;

    // Current event loaded from NVS
    CriticalEventLog current_event_;

    // Flag indicating if an event exists in NVS
    bool event_exists_{false};

    // NVS key for storing critical event
    static constexpr const char* NVS_KEY = "critical_event";
};

} // namespace persistent_state_manager
} // namespace esphome
