#pragma once

#include "led_behavior.h"

namespace plantos_controller {

/**
 * FEED_FILLING State LED Behavior
 * Visual: Solid blue (same as WATER_FILLING)
 * Duration: Until water level HIGH sensor triggers or timeout
 */
class FeedFillingBehavior : public LedBehavior {
public:
    void update(plantos_hal::HAL* hal, uint32_t elapsed) override;
};

} // namespace plantos_controller
