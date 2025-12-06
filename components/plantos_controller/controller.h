#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/plantos_controller/led_behaviors/led_behavior.h"
#include <memory>
#include <string>

// Forward declarations
namespace plantos_hal {
class HAL;
}

namespace esphome {
namespace actuator_safety_gate {
class ActuatorSafetyGate;
}
}

namespace plantos_controller {

/**
 * ControllerState - Unified FSM states for PlantOS Controller
 *
 * This replaces the old controller's states with a unified architecture.
 * LED behaviors are mapped to these states in LedBehaviorSystem.
 */
enum class ControllerState {
    INIT,              // Boot sequence (Red→Yellow→Green)
    IDLE,              // Ready state (Breathing Green)
    MAINTENANCE,       // Manual maintenance mode (Solid Yellow)
    ERROR,             // Error condition (Fast Red Flash)
    PH_MEASURING,      // pH stabilization phase (Yellow Pulse)
    PH_CALCULATING,    // Determining pH adjustment (Yellow Fast Blink)
    PH_INJECTING,      // Acid dosing in progress (Cyan Pulse)
    PH_MIXING,         // Air pump mixing after injection (Blue Pulse)
    PH_CALIBRATING,    // pH sensor calibration (Yellow Fast Blink)
    FEEDING,           // Nutrient dosing (Orange Pulse)
    WATER_FILLING,     // Fresh water addition (Blue Solid)
    WATER_EMPTYING     // Wastewater removal (Purple Pulse)
};

/**
 * PlantOSController - Unified Controller (Layer 1)
 *
 * Unified architecture replaces old Controller component:
 * - Uses HAL for all hardware access (sensors, LED)
 * - Delegates actuator control to SafetyGate → HAL
 * - LED patterns via LedBehaviorSystem
 * - Dependency injection for all subsystems
 *
 * Layer 1 (Controller) → Layer 2 (SafetyGate) → Layer 3 (HAL) → Hardware
 */
class PlantOSController : public esphome::Component {
public:
    PlantOSController();

    // ========================================================================
    // ESPHome Component Lifecycle
    // ========================================================================

    void setup() override;
    void loop() override;

    // ========================================================================
    // Dependency Injection (called from Python __init__.py)
    // ========================================================================

    /**
     * Set Hardware Abstraction Layer
     * REQUIRED - Controller needs HAL for sensors and LED
     */
    void setHAL(plantos_hal::HAL* hal) { hal_ = hal; }

    /**
     * Set Actuator Safety Gate
     * REQUIRED - Controller delegates all actuator control to SafetyGate
     */
    void setSafetyGate(esphome::actuator_safety_gate::ActuatorSafetyGate* gate) {
        safety_gate_ = gate;
    }

    // ========================================================================
    // Public API for External Control (ESPHome services, buttons, etc.)
    // ========================================================================

    /**
     * Start pH correction sequence
     * Transitions to PH_MEASURING state
     */
    void startPhCorrection();

    /**
     * Start feeding sequence
     * Transitions to FEEDING state
     */
    void startFeeding();

    /**
     * Start water fill operation
     * Transitions to WATER_FILLING state
     */
    void startFillTank();

    /**
     * Start water empty operation
     * Transitions to WATER_EMPTYING state
     */
    void startEmptyTank();

    /**
     * Toggle maintenance mode
     * @param enable true to enter maintenance, false to exit
     * @return true if state changed
     */
    bool toggleMaintenanceMode(bool enable);

    /**
     * Reset controller to INIT state
     * Used for error recovery
     */
    void resetToInit();

    /**
     * Get current controller state
     * @return Current ControllerState
     */
    ControllerState getCurrentState() const { return current_state_; }

private:
    // ========================================================================
    // Dependencies
    // ========================================================================

    plantos_hal::HAL* hal_{nullptr};
    esphome::actuator_safety_gate::ActuatorSafetyGate* safety_gate_{nullptr};

    // ========================================================================
    // FSM State Management
    // ========================================================================

    ControllerState current_state_{ControllerState::INIT};
    uint32_t state_start_time_{0};        // millis() when state started
    uint32_t state_counter_{0};           // General-purpose counter for states

    // ========================================================================
    // LED Behavior System
    // ========================================================================

    std::unique_ptr<LedBehaviorSystem> led_behaviors_;

    // ========================================================================
    // State Handlers (called from loop())
    // ========================================================================

    void handleInit();
    void handleIdle();
    void handleMaintenance();
    void handleError();
    void handlePhMeasuring();
    void handlePhCalculating();
    void handlePhInjecting();
    void handlePhMixing();
    void handlePhCalibrating();
    void handleFeeding();
    void handleWaterFilling();
    void handleWaterEmptying();

    // ========================================================================
    // State Transition
    // ========================================================================

    /**
     * Transition to new state
     * Updates state tracking and triggers LED behavior change
     */
    void transitionTo(ControllerState newState);

    /**
     * Get elapsed time in current state (milliseconds)
     */
    uint32_t getStateElapsed() const;

    // ========================================================================
    // Actuator Control Helpers (Controller → SafetyGate → HAL)
    // ========================================================================

    /**
     * Request pump operation via SafetyGate
     * @param pumpId Pump identifier (e.g., "AcidPump")
     * @param state true=ON, false=OFF
     * @param durationSec Maximum duration in seconds (0=no limit)
     * @return true if approved by SafetyGate
     */
    bool requestPump(const std::string& pumpId, bool state, uint32_t durationSec = 0);

    /**
     * Request valve operation via SafetyGate
     * @param valveId Valve identifier (e.g., "WaterValve")
     * @param state true=OPEN, false=CLOSED
     * @param durationSec Maximum duration in seconds (0=no limit)
     * @return true if approved by SafetyGate
     */
    bool requestValve(const std::string& valveId, bool state, uint32_t durationSec = 0);

    /**
     * Emergency stop - turn off all pumps
     * Used in error conditions and maintenance mode
     */
    void turnOffAllPumps();

    // ========================================================================
    // Sensor Helpers (Controller → HAL)
    // ========================================================================

    /**
     * Read current pH value from HAL
     * @return pH value (0.0 if not available)
     */
    float readPH();

    /**
     * Check if pH sensor has valid reading
     */
    bool hasPhValue();

    // ========================================================================
    // Constants
    // ========================================================================

    static constexpr const char* TAG = "plantos_controller";

    // State timeouts (milliseconds)
    static constexpr uint32_t INIT_DURATION = 3000;           // 3 seconds boot
    static constexpr uint32_t ERROR_DURATION = 5000;          // 5 seconds error display
    static constexpr uint32_t PH_MEASURING_DURATION = 300000; // 5 minutes stabilization
    static constexpr uint32_t PH_MIXING_DURATION = 120000;    // 2 minutes mixing

    // pH correction parameters
    static constexpr float PH_TARGET_MIN = 5.5f;
    static constexpr float PH_TARGET_MAX = 6.5f;
    static constexpr uint32_t PH_MAX_ITERATIONS = 5;

    // Actuator IDs (must match SafetyGate configuration)
    static constexpr const char* ACID_PUMP = "AcidPump";
    static constexpr const char* NUTRIENT_PUMP_A = "NutrientPumpA";
    static constexpr const char* NUTRIENT_PUMP_B = "NutrientPumpB";
    static constexpr const char* NUTRIENT_PUMP_C = "NutrientPumpC";
    static constexpr const char* WATER_VALVE = "WaterValve";
    static constexpr const char* WASTEWATER_PUMP = "WastewaterPump";
    static constexpr const char* AIR_PUMP = "AirPump";
};

} // namespace plantos_controller
