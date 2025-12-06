#pragma once

#include "led_behavior.h"

namespace plantos_controller {

/**
 * BootSequenceBehavior - Red → Yellow → Green boot sequence
 *
 * Visual feedback during system initialization (3 seconds total):
 * - 0-1s: Red (initializing)
 * - 1-2s: Yellow (loading)
 * - 2-3s: Green (ready)
 *
 * Behavior completes after 3 seconds.
 */
class BootSequenceBehavior : public LedBehavior {
public:
    void update(plantos_hal::HAL* hal, uint32_t elapsed) override;
    bool isComplete(uint32_t elapsed) const override;

private:
    static constexpr uint32_t DURATION_MS = 3000;  // Total duration: 3 seconds
};

} // namespace plantos_controller
