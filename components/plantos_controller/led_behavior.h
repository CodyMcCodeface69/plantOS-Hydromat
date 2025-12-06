#pragma once

#include <memory>
#include <map>
#include <cstdint>

// Forward declarations
namespace plantos_hal {
class HAL;
}

namespace plantos_controller {

// Forward declaration of ControllerState (will be defined in controller.h)
enum class ControllerState;

/**
 * LedBehavior - Base class for LED visual behaviors
 *
 * LED patterns are visual behaviors triggered by Controller states, not separate
 * FSM states. Each behavior runs at ~1000 Hz for smooth animations via HAL.
 *
 * Behaviors implement the update() method to set LED colors/brightness based on
 * elapsed time since the behavior started.
 */
class LedBehavior {
public:
    virtual ~LedBehavior() = default;

    /**
     * Called when this behavior becomes active
     * Use this to initialize any state needed for the behavior
     */
    virtual void onEnter() {}

    /**
     * Update LED state based on elapsed time
     *
     * @param hal Pointer to HAL for LED control
     * @param elapsed Milliseconds elapsed since behavior started
     *
     * Called every loop iteration (~1000 Hz) for smooth animations.
     * Implementation should call hal->setSystemLED() with calculated color/brightness.
     */
    virtual void update(plantos_hal::HAL* hal, uint32_t elapsed) = 0;

    /**
     * Called when this behavior is about to be replaced
     * Use this to clean up or ensure LED is in a known state
     */
    virtual void onExit() {}

    /**
     * Check if this behavior has completed
     *
     * @param elapsed Milliseconds elapsed since behavior started
     * @return true if behavior is complete and should transition
     *
     * Most behaviors are continuous (breathing, pulsing) and never complete.
     * Boot sequence and error states may have fixed durations.
     */
    virtual bool isComplete(uint32_t elapsed) const { return false; }
};

/**
 * LedBehaviorSystem - Manages LED behaviors for controller states
 *
 * Maps controller states to LED behaviors and handles transitions.
 * Called from Controller::loop() to update LED based on current state.
 */
class LedBehaviorSystem {
public:
    LedBehaviorSystem();

    /**
     * Update LED behavior for current controller state
     *
     * @param state Current controller state
     * @param stateElapsed Milliseconds elapsed in current state
     * @param hal Pointer to HAL for LED control
     *
     * If state has changed, transitions to new behavior and calls onEnter().
     * Calls current behavior's update() method with elapsed time.
     */
    void update(ControllerState state, uint32_t stateElapsed, plantos_hal::HAL* hal);

    /**
     * Manually set a behavior for a specific state
     *
     * @param state Controller state
     * @param behavior Unique pointer to behavior (takes ownership)
     *
     * Used for testing or custom behavior injection.
     * Default behaviors are set in constructor.
     */
    void setBehavior(ControllerState state, std::unique_ptr<LedBehavior> behavior);

private:
    // Current active state (for detecting transitions)
    ControllerState current_state_;

    // Current active behavior (owned by this system)
    std::unique_ptr<LedBehavior> current_behavior_;

    // Map of states to behaviors (default behaviors set in constructor)
    std::map<ControllerState, std::unique_ptr<LedBehavior>> state_behaviors_;

    // Helper to transition to new behavior
    void transitionToBehavior(ControllerState newState);
};

} // namespace plantos_controller
