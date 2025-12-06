#include "solid_yellow.h"
#include "esphome/components/plantos_hal/hal.h"

namespace plantos_controller {

void SolidYellowBehavior::update(plantos_hal::HAL* hal, uint32_t elapsed) {
    if (!hal) return;
    hal->setSystemLED(1.0f, 1.0f, 0.0f);  // Solid yellow
}

} // namespace plantos_controller
