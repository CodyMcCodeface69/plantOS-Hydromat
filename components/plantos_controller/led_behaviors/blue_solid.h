#pragma once

#include "led_behavior.h"

namespace plantos_controller {

class BlueSolidBehavior : public LedBehavior {
public:
    void update(plantos_hal::HAL* hal, uint32_t elapsed) override;
};

} // namespace plantos_controller
