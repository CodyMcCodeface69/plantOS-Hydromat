#pragma once

#include "led_behavior.h"

namespace plantos_controller {

/**
 * SolidOrangeBehavior - Solid orange LED
 *
 * Visual feedback for PAUSE state.
 * Continuous behavior (never completes).
 */
class SolidOrangeBehavior : public LedBehavior {
public:
    void update(plantos_hal::HAL* hal, uint32_t elapsed) override;
};

} // namespace plantos_controller
