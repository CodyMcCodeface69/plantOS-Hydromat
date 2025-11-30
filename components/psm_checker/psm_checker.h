#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/persistent_state_manager/persistent_state_manager.h"
#include "esphome/components/controller/controller.h"

namespace esphome {
namespace psm_checker {

/**
 * PSMChecker - Test Component for Persistent State Manager
 *
 * This component validates the Persistent State Manager by:
 * 1. On first boot: Logs a test event and prompts user to unplug
 * 2. On reboot: Checks if the event was recovered from NVS
 * 3. Triggers controller ERROR_TEST state for visual feedback
 *
 * Test Sequence:
 * - Boot 1: Log "PSM_TEST" event, enter ERROR_TEST state, prompt to unplug
 * - Boot 2: Detect recovered event, verify timestamp, clear event
 */
class PSMChecker : public Component {
public:
    PSMChecker();

    void setup() override;
    void loop() override;

    /**
     * Set the persistent state manager to test
     * @param psm Pointer to PersistentStateManager instance
     */
    void set_psm(persistent_state_manager::PersistentStateManager *psm) {
        psm_ = psm;
    }

    /**
     * Set the controller to trigger ERROR_TEST state
     * @param controller Pointer to Controller instance
     */
    void set_controller(controller::Controller *controller) {
        controller_ = controller;
    }

    /**
     * Set the test interval (how often to run test)
     * @param interval_ms Interval in milliseconds (0 = run once on boot)
     */
    void set_test_interval(uint32_t interval_ms) {
        test_interval_ = interval_ms;
    }

private:
    persistent_state_manager::PersistentStateManager *psm_{nullptr};
    controller::Controller *controller_{nullptr};
    uint32_t test_interval_{0};  // Default: run once on boot
    uint32_t last_test_time_{0};
    uint32_t boot_time_{0};      // Track when component was initialized
    bool test_executed_{false};
    bool recovery_checked_{false};

    // Delay for PSM messages (10 seconds after boot)
    static constexpr uint32_t PSM_MESSAGE_DELAY_MS = 10000;

    // Test methods
    void check_recovery();
    void trigger_test();
};

} // namespace psm_checker
} // namespace esphome
