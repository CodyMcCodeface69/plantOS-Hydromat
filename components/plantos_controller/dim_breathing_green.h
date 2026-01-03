#pragma once

#include "led_behavior.h"

namespace plantos_controller {

/**
 * DimBreathingGreenBehavior - Dimmed smooth sine wave breathing animation
 *
 * Visual feedback for NIGHT state - system is in night mode.
 * Green LED pulses smoothly using sine wave (5% to 30% brightness).
 *
 * Continuous behavior (never completes).
 */
class DimBreathingGreenBehavior : public LedBehavior {
public:
    void update(plantos_hal::HAL* hal, uint32_t elapsed) override;

private:
    static constexpr float PI = 3.14159265359f;
    static constexpr float MIN_BRIGHTNESS = 0.05f;  // 5% minimum (dimmer than IDLE)
    static constexpr float MAX_BRIGHTNESS = 0.30f;  // 30% maximum (dimmer than IDLE)
};

} // namespace plantos_controller
