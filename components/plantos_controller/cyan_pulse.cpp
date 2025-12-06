#include "cyan_pulse.h"
#include "esphome/components/plantos_hal/hal.h"
#include <cmath>

namespace plantos_controller {

void CyanPulseBehavior::update(plantos_hal::HAL* hal, uint32_t elapsed) {
    if (!hal) return;

    float t = elapsed / 1000.0f;
    float sineValue = std::sin(t * 2.0f * PI * FREQUENCY);

    float brightness = (sineValue + 1.0f) / 2.0f;
    brightness = MIN_BRIGHTNESS + (brightness * (MAX_BRIGHTNESS - MIN_BRIGHTNESS));

    hal->setSystemLED(0.0f, 1.0f, 1.0f, brightness);  // Cyan
}

} // namespace plantos_controller
