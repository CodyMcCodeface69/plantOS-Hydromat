#pragma once

#include "led_behavior.h"

namespace plantos_controller {

/**
 * YellowFastBlinkBehavior - Fast yellow blink (2 Hz)
 *
 * Visual feedback for PH_CALCULATING state.
 * Yellow LED blinks at 2 Hz (500ms cycle).
 *
 * Continuous behavior (never completes).
 */
class YellowFastBlinkBehavior : public LedBehavior {
public:
    void update(plantos_hal::HAL* hal, uint32_t elapsed) override;

private:
    static constexpr uint32_t CYCLE_MS = 500;  // 2 Hz = 500ms per cycle
};

} // namespace plantos_controller
