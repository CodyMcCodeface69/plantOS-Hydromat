#include "persistent_state_manager.h"

namespace esphome {
namespace persistent_state_manager {

static const char *TAG = "psm";

PersistentStateManager::PersistentStateManager() {
}

void PersistentStateManager::setup() {
    ESP_LOGW(TAG, "========================================");
    ESP_LOGW(TAG, "  PSM SETUP STARTING");
    ESP_LOGW(TAG, "========================================");
    ESP_LOGI(TAG, "Initializing Persistent State Manager");

    // Initialize NVS preference object
    // The hash is used to create a unique namespace for this preference
    pref_ = global_preferences->make_preference<CriticalEventLog>(
        fnv1_hash(NVS_KEY), true);

    ESP_LOGI(TAG, "NVS preference object created (key: %s)", NVS_KEY);

    // Attempt to load existing event from NVS
    CriticalEventLog loaded_event;
    if (pref_.load(&loaded_event)) {
        // Successfully loaded an event from NVS
        current_event_ = loaded_event;
        event_exists_ = true;

        // Calculate event age
        int64_t current_time = time(nullptr);
        int64_t age_seconds = current_time - current_event_.timestampSec;

        ESP_LOGW(TAG, "========================================");
        ESP_LOGW(TAG, "  CRITICAL EVENT FOUND IN NVS");
        ESP_LOGW(TAG, "========================================");
        ESP_LOGW(TAG, "Event ID: %s", current_event_.eventID);
        ESP_LOGW(TAG, "Status: %d", current_event_.status);
        ESP_LOGW(TAG, "Timestamp: %lld", (long long)current_event_.timestampSec);
        ESP_LOGW(TAG, "Event Age: %lld seconds", (long long)age_seconds);
        ESP_LOGW(TAG, "");
        ESP_LOGW(TAG, "This may indicate an interrupted operation!");
        ESP_LOGW(TAG, "Please check if recovery action is needed.");
        ESP_LOGW(TAG, "========================================");
    } else {
        // No event found - clean boot
        event_exists_ = false;
        ESP_LOGI(TAG, "No critical event found in NVS - clean boot");
    }

    ESP_LOGW(TAG, "========================================");
    ESP_LOGW(TAG, "  PSM SETUP COMPLETE");
    ESP_LOGW(TAG, "========================================");
    ESP_LOGI(TAG, "Persistent State Manager initialized");
}

void PersistentStateManager::logEvent(const char* id, int status) {
    if (id == nullptr) {
        ESP_LOGE(TAG, "Cannot log event: ID is null");
        return;
    }

    // Get current time from NTP
    int64_t current_time = time(nullptr);

    // Check if time is synchronized (year > 2020)
    if (current_time < 1577836800) {  // Jan 1, 2020
        ESP_LOGW(TAG, "NTP not synchronized - using system uptime for timestamp");
        current_time = millis() / 1000;  // Use uptime as fallback
    }

    // Log what we're ABOUT to save (before overwriting)
    if (event_exists_) {
        ESP_LOGW(TAG, "===============================================");
        ESP_LOGW(TAG, "PSM OVERWRITE DETECTED!");
        ESP_LOGW(TAG, "  OLD Event: '%s' (status: %d)", current_event_.eventID, current_event_.status);
        ESP_LOGW(TAG, "  NEW Event: '%s' (status: %d)", id, status);
        ESP_LOGW(TAG, "===============================================");
    } else {
        ESP_LOGI(TAG, ">>> PSM: Writing NEW event '%s' (status: %d)", id, status);
    }

    // Create new event log
    CriticalEventLog new_event(id, current_time, status);
    current_event_ = new_event;
    event_exists_ = true;

    // Save to NVS immediately
    if (pref_.save(&current_event_)) {
        ESP_LOGI(TAG, "Event logged to NVS:");
        ESP_LOGI(TAG, "  ID: %s", current_event_.eventID);
        ESP_LOGI(TAG, "  Status: %d", current_event_.status);
        ESP_LOGI(TAG, "  Timestamp: %lld", (long long)current_event_.timestampSec);

        // CRITICAL: Commit to flash immediately
        // pref_.save() only queues the write - sync() actually commits to NVS
        ESP_LOGW(TAG, ">>> COMMITTING TO FLASH: Calling global_preferences->sync()");
        if (global_preferences->sync()) {
            ESP_LOGW(TAG, ">>> FLASH COMMIT SUCCESS");
        } else {
            ESP_LOGE(TAG, ">>> FLASH COMMIT FAILED!");
        }

        // CRITICAL FIX: Removed 200ms blocking delay that could contribute to WDT timeouts
        // sync() should be synchronous and wait for completion internally
        // Critical for STATE_SHUTDOWN and STATE_PAUSE persistence
        if (strcmp(id, "STATE_SHUTDOWN") == 0 || strcmp(id, "STATE_PAUSE") == 0) {
            // Verify what was actually written to NVS
            CriticalEventLog verify_event;
            if (pref_.load(&verify_event)) {
                ESP_LOGW(TAG, ">>> NVS VERIFICATION READ: EventID='%s', Status=%d",
                         verify_event.eventID, verify_event.status);

                if (strcmp(verify_event.eventID, id) != 0) {
                    ESP_LOGE(TAG, ">>> NVS CORRUPTION DETECTED!");
                    ESP_LOGE(TAG, "    Expected: '%s', Got: '%s'", id, verify_event.eventID);
                }
            } else {
                ESP_LOGE(TAG, ">>> NVS VERIFICATION FAILED: Could not read back!");
            }
        }
    } else {
        ESP_LOGE(TAG, "CRITICAL: Failed to save event to NVS!");
        ESP_LOGE(TAG, "Event: %s (status: %d)", id, status);
    }
}

void PersistentStateManager::clearEvent() {
    if (!event_exists_) {
        ESP_LOGD(TAG, "No event to clear");
        return;
    }

    ESP_LOGW(TAG, "===============================================");
    ESP_LOGW(TAG, "PSM CLEAR REQUESTED!");
    ESP_LOGW(TAG, "  Clearing Event: '%s' (status: %d)", current_event_.eventID, current_event_.status);

    // Print stack trace to see who called clear
    ESP_LOGW(TAG, "  Called from: %s", __builtin_return_address(0) ? "controller" : "unknown");
    ESP_LOGW(TAG, "===============================================");

    // Reset internal state
    event_exists_ = false;
    current_event_ = CriticalEventLog();

    // Clear from NVS by saving an empty/zeroed event
    // ESPHome doesn't have a direct "delete" API, so we save zeros
    CriticalEventLog empty_event;
    if (pref_.save(&empty_event)) {
        ESP_LOGI(TAG, "Event cleared successfully from NVS");

        // CRITICAL: Commit to flash immediately
        ESP_LOGW(TAG, ">>> COMMITTING CLEAR TO FLASH: Calling global_preferences->sync()");
        if (global_preferences->sync()) {
            ESP_LOGW(TAG, ">>> FLASH COMMIT SUCCESS");
        } else {
            ESP_LOGE(TAG, ">>> FLASH COMMIT FAILED!");
        }

        // CRITICAL FIX: Removed 100ms blocking delay that could contribute to WDT timeouts
        // sync() should be synchronous and wait for completion internally

        // Verify the clear
        CriticalEventLog verify_event;
        if (pref_.load(&verify_event)) {
            if (verify_event.eventID[0] == '\0') {
                ESP_LOGI(TAG, ">>> NVS CLEAR VERIFIED: Event successfully erased");
            } else {
                ESP_LOGE(TAG, ">>> NVS CLEAR FAILED: Still contains '%s'", verify_event.eventID);
            }
        }
    } else {
        ESP_LOGE(TAG, "Failed to clear event from NVS");
    }
}

bool PersistentStateManager::wasInterrupted(int64_t maxAgeSeconds) {
    if (!event_exists_) {
        return false;  // No event logged
    }

    // Check if event ID is non-empty (valid event)
    if (current_event_.eventID[0] == '\0') {
        return false;  // Empty event ID = no real event
    }

    // Get current time
    int64_t current_time = time(nullptr);

    // Check if time is synchronized
    if (current_time < 1577836800) {  // Jan 1, 2020
        ESP_LOGW(TAG, "NTP not synchronized - cannot accurately determine event age");
        // If NTP not synced, assume event is recent if it exists
        return true;
    }

    // Calculate event age
    int64_t age_seconds = current_time - current_event_.timestampSec;

    // Check if event is within the acceptable age window
    bool is_recent = (age_seconds >= 0 && age_seconds <= maxAgeSeconds);

    ESP_LOGD(TAG, "Event age check: %lld seconds (max: %lld) - %s",
             (long long)age_seconds, (long long)maxAgeSeconds,
             is_recent ? "INTERRUPTED" : "TOO OLD");

    return is_recent;
}

int64_t PersistentStateManager::getEventAge() const {
    if (!event_exists_) {
        return -1;  // No event
    }

    int64_t current_time = time(nullptr);

    // Check if time is synchronized
    if (current_time < 1577836800) {  // Jan 1, 2020
        return -1;  // Cannot determine age without NTP
    }

    return current_time - current_event_.timestampSec;
}

bool PersistentStateManager::saveState(const char* key, bool value) {
    if (key == nullptr) {
        ESP_LOGE(TAG, "Cannot save state: key is null");
        return false;
    }

    // Create a preference object for this specific key
    // Use fnv1_hash to generate a unique hash for the key
    auto pref = global_preferences->make_preference<bool>(fnv1_hash(key), true);

    // Save the boolean value to NVS
    if (pref.save(&value)) {
        ESP_LOGD(TAG, "State saved to NVS: %s = %s", key, value ? "true" : "false");
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to save state to NVS: %s", key);
        return false;
    }
}

bool PersistentStateManager::loadState(const char* key, bool default_value) {
    if (key == nullptr) {
        ESP_LOGE(TAG, "Cannot load state: key is null");
        return default_value;
    }

    // Create a preference object for this specific key
    auto pref = global_preferences->make_preference<bool>(fnv1_hash(key), true);

    // Attempt to load the boolean value from NVS
    bool loaded_value = default_value;
    if (pref.load(&loaded_value)) {
        ESP_LOGV(TAG, "State loaded from NVS: %s = %s", key, loaded_value ? "true" : "false");
        return loaded_value;
    } else {
        ESP_LOGV(TAG, "No state found in NVS for key: %s (using default: %s)",
                 key, default_value ? "true" : "false");
        return default_value;
    }
}

} // namespace persistent_state_manager
} // namespace esphome
