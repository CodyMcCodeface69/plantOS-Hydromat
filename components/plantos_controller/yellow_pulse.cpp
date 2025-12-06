#include "yellow_pulse.h"
#include "esphome/components/plantos_hal/hal.h"
#include <cmath>

namespace plantos_controller {

void YellowPulseBehavior::update(plantos_hal::HAL* hal, uint32_t elapsed) {
    if (!hal) return;

    float t = elapsed / 1000.0f;  // Convert to seconds
    float sineValue = std::sin(t * 2.0f * PI * FREQUENCY);

    // Map sine (-1 to 1) to brightness (MIN to MAX)
    float brightness = (sineValue + 1.0f) / 2.0f;
    brightness = MIN_BRIGHTNESS + (brightness * (MAX_BRIGHTNESS - MIN_BRIGHTNESS));

    hal->setSystemLED(1.0f, 1.0f, 0.0f, brightness);  // Yellow
}

} // namespace plantos_controller
