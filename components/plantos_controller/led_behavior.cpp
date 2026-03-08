#include "led_behavior.h"
#include "esphome/components/plantos_hal/hal.h"
#include "esphome/core/log.h"

// Include all concrete behaviors
#include "boot_sequence.h"
#include "breathing_green.h"
#include "dim_breathing_green.h"
#include "solid_yellow.h"
#include "solid_orange.h"
#include "error_flash.h"
#include "yellow_pulse.h"
#include "yellow_fast_blink.h"
#include "cyan_pulse.h"
#include "blue_pulse.h"
#include "orange_pulse.h"
#include "blue_solid.h"
#include "purple_pulse.h"
#include "FeedFillingBehavior.h"

namespace plantos_controller {

static const char* TAG = "led_behavior";

// We need to define ControllerState here for the implementation
// This will match the definition in controller.h
enum class ControllerState {
    INIT,
    IDLE,
    NIGHT,
    SHUTDOWN,
    PAUSE,
    ERROR,
    PH_PROCESSING,
    PH_MEASURING,
    PH_CALCULATING,
    PH_INJECTING,
    PH_MIXING,
    PH_CALIBRATING,
    FEEDING,
    WATER_FILLING,
    WATER_EMPTYING,
    FEED_FILLING,
    EC_PROCESSING,
    EC_CALCULATING,
    EC_FEEDING,
    EC_MIXING,
    EC_MEASURING,
    EC_CALIBRATING
};

LedBehaviorSystem::LedBehaviorSystem()
    : current_state_(ControllerState::INIT) {

    // Initialize default behaviors for each state
    // These will be created on-demand in transitionToBehavior()
    ESP_LOGI(TAG, "LED Behavior System initialized");
}

void LedBehaviorSystem::update(ControllerState state, uint32_t stateElapsed, plantos_hal::HAL* hal) {
    if (!hal) {
        return; // No HAL, cannot control LED
    }

    // Check if state changed - if so, transition to new behavior
    if (state != current_state_) {
        transitionToBehavior(state);
        current_state_ = state;
    }

    // Update current behavior
    if (current_behavior_) {
        current_behavior_->update(hal, stateElapsed);

        // Check if behavior is complete (e.g., boot sequence)
        if (current_behavior_->isComplete(stateElapsed)) {
            ESP_LOGD(TAG, "Behavior complete");
            // Behavior will naturally transition when state changes
        }
    }
}

void LedBehaviorSystem::setBehavior(ControllerState state, std::unique_ptr<LedBehavior> behavior) {
    state_behaviors_[state] = std::move(behavior);
}

void LedBehaviorSystem::transitionToBehavior(ControllerState newState) {
    // Call onExit on current behavior
    if (current_behavior_) {
        current_behavior_->onExit();
    }

    // Create or get behavior for new state
    std::unique_ptr<LedBehavior> newBehavior;

    // Check if we have a custom behavior for this state
    auto it = state_behaviors_.find(newState);
    if (it != state_behaviors_.end() && it->second) {
        // Use existing behavior (note: this consumes it, so it's one-time use)
        // For production, we'd clone or recreate behaviors
        ESP_LOGD(TAG, "Using custom behavior for state");
        // For now, we'll just create default behaviors below
    }

    // Create default behavior based on state
    switch (newState) {
        case ControllerState::INIT:
            newBehavior = std::make_unique<BootSequenceBehavior>();
            ESP_LOGI(TAG, "LED: Boot Sequence (R→Y→G)");
            break;

        case ControllerState::IDLE:
            newBehavior = std::make_unique<SolidYellowBehavior>();
            ESP_LOGI(TAG, "LED: Solid Yellow (Idle)");
            break;

        case ControllerState::NIGHT:
            newBehavior = std::make_unique<SolidYellowBehavior>();
            ESP_LOGI(TAG, "LED: Solid Yellow (Night Mode)");
            break;

        case ControllerState::SHUTDOWN:
            newBehavior = std::make_unique<SolidYellowBehavior>();
            ESP_LOGI(TAG, "LED: Solid Yellow (Shutdown)");
            break;

        case ControllerState::PAUSE:
            newBehavior = std::make_unique<SolidOrangeBehavior>();
            ESP_LOGI(TAG, "LED: Solid Orange (Pause)");
            break;

        case ControllerState::ERROR:
            newBehavior = std::make_unique<ErrorFlashBehavior>();
            ESP_LOGI(TAG, "LED: Error Flash (Red)");
            break;

        case ControllerState::PH_PROCESSING:
            newBehavior = std::make_unique<YellowPulseBehavior>();
            ESP_LOGI(TAG, "LED: Yellow Pulse (pH Processing)");
            break;

        case ControllerState::PH_MEASURING:
            newBehavior = std::make_unique<YellowPulseBehavior>();
            ESP_LOGI(TAG, "LED: Yellow Pulse (pH Measuring)");
            break;

        case ControllerState::PH_CALCULATING:
            newBehavior = std::make_unique<YellowFastBlinkBehavior>();
            ESP_LOGI(TAG, "LED: Yellow Fast Blink (pH Calculating)");
            break;

        case ControllerState::PH_INJECTING:
            newBehavior = std::make_unique<CyanPulseBehavior>();
            ESP_LOGI(TAG, "LED: Cyan Pulse (pH Injecting)");
            break;

        case ControllerState::PH_MIXING:
            newBehavior = std::make_unique<BluePulseBehavior>();
            ESP_LOGI(TAG, "LED: Blue Pulse (pH Mixing)");
            break;

        case ControllerState::PH_CALIBRATING:
            newBehavior = std::make_unique<YellowFastBlinkBehavior>();
            ESP_LOGI(TAG, "LED: Yellow Fast Blink (pH Calibrating)");
            break;

        case ControllerState::FEEDING:
            newBehavior = std::make_unique<OrangePulseBehavior>();
            ESP_LOGI(TAG, "LED: Orange Pulse (Feeding)");
            break;

        case ControllerState::WATER_FILLING:
            newBehavior = std::make_unique<BlueSolidBehavior>();
            ESP_LOGI(TAG, "LED: Blue Solid (Water Filling)");
            break;

        case ControllerState::WATER_EMPTYING:
            newBehavior = std::make_unique<PurplePulseBehavior>();
            ESP_LOGI(TAG, "LED: Purple Pulse (Water Emptying)");
            break;

        case ControllerState::FEED_FILLING:
            newBehavior = std::make_unique<FeedFillingBehavior>();
            ESP_LOGI(TAG, "LED: Blue Solid (Feed Filling)");
            break;

        case ControllerState::EC_PROCESSING:
            newBehavior = std::make_unique<YellowPulseBehavior>();
            ESP_LOGI(TAG, "LED: Yellow Pulse (EC Processing)");
            break;

        case ControllerState::EC_CALCULATING:
            newBehavior = std::make_unique<YellowFastBlinkBehavior>();
            ESP_LOGI(TAG, "LED: Yellow Fast Blink (EC Calculating)");
            break;

        case ControllerState::EC_FEEDING:
            newBehavior = std::make_unique<OrangePulseBehavior>();
            ESP_LOGI(TAG, "LED: Orange Pulse (EC Feeding)");
            break;

        case ControllerState::EC_MIXING:
            newBehavior = std::make_unique<BluePulseBehavior>();
            ESP_LOGI(TAG, "LED: Blue Pulse (EC Mixing)");
            break;

        case ControllerState::EC_MEASURING:
            newBehavior = std::make_unique<YellowPulseBehavior>();
            ESP_LOGI(TAG, "LED: Yellow Pulse (EC Measuring)");
            break;

        case ControllerState::EC_CALIBRATING:
            newBehavior = std::make_unique<YellowFastBlinkBehavior>();
            ESP_LOGI(TAG, "LED: Yellow Fast Blink (EC Calibrating)");
            break;

        default:
            // Unknown state - use breathing green as safe default
            newBehavior = std::make_unique<BreathingGreenBehavior>();
            ESP_LOGW(TAG, "Unknown state - using default breathing green");
            break;
    }

    // Set new behavior and call onEnter
    current_behavior_ = std::move(newBehavior);
    if (current_behavior_) {
        current_behavior_->onEnter();
    }
}

} // namespace plantos_controller
