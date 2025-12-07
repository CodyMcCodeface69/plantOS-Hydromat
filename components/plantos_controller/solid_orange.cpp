#include "solid_orange.h"
#include "esphome/components/plantos_hal/hal.h"

namespace plantos_controller {

void SolidOrangeBehavior::update(plantos_hal::HAL* hal, uint32_t elapsed) {
    // Solid orange color for PAUSE state
    // RGB: (255, 128, 0) normalized to (1.0, 0.5, 0.0)
    hal->setSystemLED(1.0f, 0.5f, 0.0f, 1.0f);
}

} // namespace plantos_controller
