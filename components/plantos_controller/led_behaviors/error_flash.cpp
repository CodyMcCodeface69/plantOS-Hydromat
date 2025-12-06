#include "error_flash.h"
#include "esphome/components/plantos_hal/hal.h"

namespace plantos_controller {

void ErrorFlashBehavior::update(plantos_hal::HAL* hal, uint32_t elapsed) {
    if (!hal) return;

    // Flash at 5 Hz: ON for first half of cycle, OFF for second half
    bool on = (elapsed % CYCLE_MS) < (CYCLE_MS / 2);

    if (on) {
        hal->setSystemLED(1.0f, 0.0f, 0.0f);  // Red
    } else {
        hal->turnOffLED();
    }
}

} // namespace plantos_controller
