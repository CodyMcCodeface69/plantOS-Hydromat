#include "blue_solid.h"
#include "esphome/components/plantos_hal/hal.h"

namespace plantos_controller {

void BlueSolidBehavior::update(plantos_hal::HAL* hal, uint32_t elapsed) {
    if (!hal) return;
    hal->setSystemLED(0.0f, 0.0f, 1.0f);  // Solid blue
}

} // namespace plantos_controller
