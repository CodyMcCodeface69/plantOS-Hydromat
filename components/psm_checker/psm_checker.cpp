#include "psm_checker.h"

namespace esphome {
namespace psm_checker {

static const char *TAG = "psm.checker";

PSMChecker::PSMChecker() {
}

void PSMChecker::setup() {
    ESP_LOGI(TAG, "PSMChecker initialized - messages will appear in 10 seconds");

    // Validate dependencies
    if (psm_ == nullptr) {
        ESP_LOGE(TAG, "ERROR: PSM not configured! Tests will not run.");
        return;
    }

    if (controller_ == nullptr) {
        ESP_LOGW(TAG, "WARNING: Controller not configured - ERROR_TEST state cannot be triggered");
    }

    // Track boot time for delayed message display
    boot_time_ = millis();
    last_test_time_ = millis();
}

void PSMChecker::loop() {
    // Only run if dependencies are configured
    if (psm_ == nullptr) return;

    uint32_t current_time = millis();
    uint32_t time_since_boot = current_time - boot_time_;

    // Wait for 10 seconds after boot before executing recovery check or test
    if (time_since_boot < PSM_MESSAGE_DELAY_MS) {
        return;
    }

    // Execute recovery check once after delay
    if (!recovery_checked_) {
        check_recovery();
        recovery_checked_ = true;
        return;  // Return to let status logger print in next cycle
    }

    // If test_interval is 0, only run once on boot (after delay)
    if (test_interval_ == 0) {
        if (!test_executed_) {
            trigger_test();
            test_executed_ = true;
        }
        return;
    }

    // Periodic testing (after initial delay)
    if (current_time - last_test_time_ >= test_interval_) {
        trigger_test();
        last_test_time_ = current_time;
    }
}

void PSMChecker::check_recovery() {
    // Check if an event was recovered
    if (psm_->hasEvent()) {
        auto event = psm_->getLastEvent();

        // Check if it was our PSM_TEST event
        if (strcmp(event.eventID, "PSM_TEST") == 0) {
            // Add success alert to status logger
            if (controller_ != nullptr) {
                // Check if event is recent (within 5 minutes)
                if (psm_->wasInterrupted(300)) {
                    int64_t age = psm_->getEventAge();
                    char alert_msg[200];
                    snprintf(alert_msg, sizeof(alert_msg),
                             "✓ PSM TEST SUCCESSFUL! Event recovered from NVS after reboot (age: %lld sec)",
                             (long long)age);
                    controller_->get_logger()->updateAlertStatus("PSM_TEST_OK", alert_msg);
                } else {
                    int64_t age = psm_->getEventAge();
                    char alert_msg[200];
                    snprintf(alert_msg, sizeof(alert_msg),
                             "⚠ PSM event recovered but OLD (%lld sec) - may be from previous test",
                             (long long)age);
                    controller_->get_logger()->updateAlertStatus("PSM_TEST_WARNING", alert_msg);
                }
            }

            // Clear the test event
            ESP_LOGI(TAG, "Clearing PSM_TEST event from NVS...");
            psm_->clearEvent();

            // Clear alert after 5 seconds to let it show in status report
            // The alert will be visible in the next status log cycle

        } else {
            // Different event - real interrupted operation!
            if (controller_ != nullptr) {
                char alert_msg[200];
                snprintf(alert_msg, sizeof(alert_msg),
                         "CRITICAL: Real event recovered! ID=%s Status=%d - Operation may have been interrupted!",
                         event.eventID, event.status);
                controller_->get_logger()->updateAlertStatus("PSM_RECOVERY", alert_msg);
            }
            ESP_LOGW(TAG, "Real interrupted operation detected: %s (status: %d)",
                     event.eventID, event.status);
        }

    } else {
        // No event found - clean boot
        ESP_LOGI(TAG, "No PSM event found in NVS - clean boot");
    }
}

void PSMChecker::trigger_test() {
    // Don't trigger test if we already recovered an event on this boot
    if (psm_->hasEvent()) {
        ESP_LOGD(TAG, "Skipping test trigger - event already exists (recovered on boot)");
        return;
    }

    // Log test event to NVS
    ESP_LOGI(TAG, "Logging PSM_TEST event to NVS...");
    psm_->logEvent("PSM_TEST", 0);  // Status 0 = STARTED

    // Trigger controller ERROR_TEST state if available
    if (controller_ != nullptr) {
        ESP_LOGI(TAG, "Triggering ERROR_TEST state (blue/purple pulsing LED)...");
        controller_->trigger_error_test();

        // Add alert to status logger prompting user to unplug
        controller_->get_logger()->updateAlertStatus("PSM_TEST_UNPLUG",
            "⚡ PLEASE UNPLUG DEVICE NOW! PSM test event logged. Unplug to simulate crash, then replug to verify recovery.");
    }

    ESP_LOGI(TAG, "PSM test initiated - waiting for user to unplug device");
}

} // namespace psm_checker
} // namespace esphome
