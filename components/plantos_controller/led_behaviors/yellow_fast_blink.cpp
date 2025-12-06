#include "yellow_fast_blink.h"
#include "esphome/components/plantos_hal/hal.h"

namespace plantos_controller {

void YellowFastBlinkBehavior::update(plantos_hal::HAL* hal, uint32_t elapsed) {
    if (!hal) return;

    bool on = (elapsed % CYCLE_MS) < (CYCLE_MS / 2);

    if (on) {
        hal->setSystemLED(1.0f, 1.0f, 0.0f);  // Yellow
    } else {
        hal->turnOffLED();
    }
}

} // namespace plantos_controller
