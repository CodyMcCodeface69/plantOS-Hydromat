#include "boot_sequence.h"
#include "esphome/components/plantos_hal/hal.h"

namespace plantos_controller {

void BootSequenceBehavior::update(plantos_hal::HAL* hal, uint32_t elapsed) {
    if (!hal) return;

    // Red phase: 0-1000ms
    if (elapsed < 1000) {
        hal->setSystemLED(1.0f, 0.0f, 0.0f);  // Red
    }
    // Yellow phase: 1000-2000ms
    else if (elapsed < 2000) {
        hal->setSystemLED(1.0f, 1.0f, 0.0f);  // Yellow
    }
    // Green phase: 2000-3000ms
    else {
        hal->setSystemLED(0.0f, 1.0f, 0.0f);  // Green
    }
}

bool BootSequenceBehavior::isComplete(uint32_t elapsed) const {
    return elapsed >= DURATION_MS;
}

} // namespace plantos_controller
