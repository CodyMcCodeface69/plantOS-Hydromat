#pragma once

#include "led_behavior.h"

namespace plantos_controller {

class BluePulseBehavior : public LedBehavior {
public:
    void update(plantos_hal::HAL* hal, uint32_t elapsed) override;

private:
    static constexpr float PI = 3.14159265359f;
    static constexpr float MIN_BRIGHTNESS = 0.3f;
    static constexpr float MAX_BRIGHTNESS = 1.0f;
    static constexpr float FREQUENCY = 0.5f;
};

} // namespace plantos_controller
