#pragma once

#include "led_behavior.h"

namespace plantos_controller {

/**
 * BreathingGreenBehavior - Smooth sine wave breathing animation
 *
 * Visual feedback for IDLE state - system is ready and waiting.
 * Green LED pulses smoothly using sine wave (10% to 100% brightness).
 *
 * Continuous behavior (never completes).
 */
class BreathingGreenBehavior : public LedBehavior {
public:
    void update(plantos_hal::HAL* hal, uint32_t elapsed) override;

private:
    static constexpr float PI = 3.14159265359f;
    static constexpr float MIN_BRIGHTNESS = 0.1f;  // 10% minimum
    static constexpr float MAX_BRIGHTNESS = 1.0f;  // 100% maximum
};

} // namespace plantos_controller
