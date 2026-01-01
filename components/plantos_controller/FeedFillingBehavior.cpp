#include "FeedFillingBehavior.h"
#include "esphome/components/plantos_hal/hal.h"

namespace plantos_controller {

void FeedFillingBehavior::update(plantos_hal::HAL* hal, uint32_t elapsed) {
    // Solid blue - filling tank before feeding (same visual as WATER_FILLING)
    hal->setSystemLED(0.0f, 0.0f, 1.0f, 1.0f);  // Blue 100%
}

} // namespace plantos_controller
