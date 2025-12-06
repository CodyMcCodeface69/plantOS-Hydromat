#pragma once

#include "led_behavior.h"

namespace plantos_controller {

/**
 * ErrorFlashBehavior - Fast red flashing (5 Hz)
 *
 * Visual feedback for ERROR state.
 * Flashes red LED at 5 Hz (200ms cycle: 100ms on, 100ms off).
 *
 * Continuous behavior (never completes - state will transition after timeout).
 */
class ErrorFlashBehavior : public LedBehavior {
public:
    void update(plantos_hal::HAL* hal, uint32_t elapsed) override;

private:
    static constexpr uint32_t CYCLE_MS = 200;  // 5 Hz = 200ms per cycle
};

} // namespace plantos_controller
