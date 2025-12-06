#include "breathing_green.h"
#include "esphome/components/plantos_hal/hal.h"
#include <cmath>

namespace plantos_controller {

void BreathingGreenBehavior::update(plantos_hal::HAL* hal, uint32_t elapsed) {
    if (!hal) return;

    // Calculate sine wave for breathing effect
    // Period: ~2 seconds per breath cycle
    float t = elapsed / 1000.0f;  // Convert to seconds
    float sineValue = std::sin(t * PI);  // sin ranges from -1 to 1

    // Map sine (-1 to 1) to brightness (0.1 to 1.0)
    float brightness = (sineValue + 1.0f) / 2.0f;  // Map to 0.0 to 1.0
    brightness = MIN_BRIGHTNESS + (brightness * (MAX_BRIGHTNESS - MIN_BRIGHTNESS));

    // Set green LED with calculated brightness
    hal->setSystemLED(0.0f, 1.0f, 0.0f, brightness);
}

} // namespace plantos_controller
