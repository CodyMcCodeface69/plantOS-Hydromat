#pragma once

#include "led_behavior.h"

namespace plantos_controller {

/**
 * SolidYellowBehavior - Solid yellow LED
 *
 * Visual feedback for MAINTENANCE state.
 * Continuous behavior (never completes).
 */
class SolidYellowBehavior : public LedBehavior {
public:
    void update(plantos_hal::HAL* hal, uint32_t elapsed) override;
};

} // namespace plantos_controller
