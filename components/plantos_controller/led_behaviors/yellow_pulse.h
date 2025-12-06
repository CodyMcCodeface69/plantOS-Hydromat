#pragma once

#include "led_behavior.h"

namespace plantos_controller {

/**
 * YellowPulseBehavior - Slow yellow pulse (0.5 Hz)
 *
 * Visual feedback for PH_MEASURING state.
 * Yellow LED pulses slowly (30% to 100% brightness).
 *
 * Continuous behavior (never completes).
 */
class YellowPulseBehavior : public LedBehavior {
public:
    void update(plantos_hal::HAL* hal, uint32_t elapsed) override;

private:
    static constexpr float PI = 3.14159265359f;
    static constexpr float MIN_BRIGHTNESS = 0.3f;
    static constexpr float MAX_BRIGHTNESS = 1.0f;
    static constexpr float FREQUENCY = 0.5f;  // 0.5 Hz = 2 seconds per cycle
};

} // namespace plantos_controller
