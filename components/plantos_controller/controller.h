#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "led_behavior.h"
#include "CentralStatusLogger.h"  // Local copy owned by controller
#include <memory>
#include <string>
#include <vector>

// Forward declarations
namespace plantos_hal {
class HAL;
}

namespace esphome {
namespace actuator_safety_gate {
class ActuatorSafetyGate;
}
namespace persistent_state_manager {
class PersistentStateManager;
}
}

// Forward declaration - full definition in .cpp file to avoid circular dependencies
namespace esphome {
namespace ezo_ph_uart {
class EZOPHUARTComponent;
}
namespace calendar_manager {
class CalendarManager;
}
namespace time {
class RealTimeClock;
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
    NIGHT,             // Night mode - no pH correction, feeding, or filling (Dim Green Breathing)
    SHUTDOWN,          // System shutdown - all actuators OFF, calendar disabled (Solid Yellow)
    PAUSE,             // System paused - actuators maintain state, calendar disabled (Solid Orange)
    ERROR,             // Error condition (Fast Red Flash)
    PH_PROCESSING,     // Processing pH reading to decide if correction needed (Yellow Pulse)
    PH_MEASURING,      // pH stabilization phase (Yellow Pulse)
    PH_CALCULATING,    // Determining pH adjustment (Yellow Fast Blink)
    PH_INJECTING,      // Acid dosing in progress (Cyan Pulse)
    PH_MIXING,         // Air pump mixing after injection (Blue Pulse)
    PH_CALIBRATING,    // pH sensor calibration (Yellow Fast Blink)
    FEEDING,           // Nutrient dosing (Orange Pulse)
    WATER_FILLING,     // Fresh water addition (Blue Solid)
    WATER_EMPTYING,    // Wastewater removal (Purple Pulse)
    FEED_FILLING       // Fill tank before feeding (part of Feed operation) (Blue Solid)
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

    /**
     * Set Persistent State Manager
     * OPTIONAL - Used for crash recovery and event persistence
     */
    void setPersistenceManager(esphome::persistent_state_manager::PersistentStateManager* psm) {
        psm_ = psm;
    }

    /**
     * Set pH Sensor Component
     * OPTIONAL - Used for direct pH sensor calibration control
     */
    void setPhSensor(esphome::ezo_ph_uart::EZOPHUARTComponent* ph_sensor) {
        ph_sensor_ = ph_sensor;
    }

    /**
     * Set Calendar Manager
     * OPTIONAL - Used for grow schedule and nutrient dosing
     */
    void setCalendarManager(esphome::calendar_manager::CalendarManager* calendar) {
        calendar_manager_ = calendar;
    }

    /**
     * Set Time Source (RealTimeClock)
     * OPTIONAL - Used for automatic day calculation based on grow start date
     */
    void setTimeSource(esphome::time::RealTimeClock* time_source) {
        time_source_ = time_source;
    }

    /**
     * Set Grow Start Date
     * OPTIONAL - Unix timestamp of day 1 of grow cycle (midnight UTC)
     * Enables automatic day calculation: current_day = (current_time - start_date) / 86400 % 120 + 1
     */
    void setGrowStartDate(int64_t timestamp) {
        grow_start_timestamp_ = timestamp;
    }

    /**
     * Set Night Mode Configuration
     * @param enabled Enable/disable night mode
     * @param start_hour Start hour (0-23)
     * @param end_hour End hour (0-23)
     */
    void setNightModeConfig(bool enabled, uint8_t start_hour, uint8_t end_hour) {
        night_mode_enabled_ = enabled;
        night_mode_start_hour_ = start_hour;
        night_mode_end_hour_ = end_hour;
    }

    /**
     * Check if currently in night mode hours
     * @return true if current time is within night mode hours
     */
    bool isNightModeHours() const;

    // ========================================================================
    // Public API for External Control (ESPHome services, buttons, etc.)
    // ========================================================================

    /**
     * Start pH correction sequence
     * Transitions to PH_MEASURING state
     */
    void startPhCorrection();

    /**
     * Start 3-point pH calibration sequence
     * Transitions to PH_CALIBRATING state
     * Calibration points: 4.00 (low), 7.00 (mid), 10.01 (high)
     */
    void startPhCalibration();

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
     * Start feed operation: Fill tank + add nutrients + pH correction
     * Sequence: FEED_FILLING → FEEDING → PH_PROCESSING
     */
    void startFeed();

    /**
     * Start complete reservoir change sequence
     * Sequence: Empty → Fill → Nutrients → pH correction
     * Uses wastewater pump (Shelly Socket 2) for automated tank drainage
     */
    void startReservoirChange();

    /**
     * Set controller to SHUTDOWN state
     * Turns off all actuators and disables time-based events
     * Persists to NVS - will resume in SHUTDOWN if power cycled
     */
    void setToShutdown();

    /**
     * Set controller to PAUSE state
     * Maintains actuator states but disables time-based events
     * Persists to NVS - will resume in PAUSE if power cycled
     */
    void setToPause();

    /**
     * Set controller to IDLE state
     * Only way to exit SHUTDOWN or PAUSE states
     * Clears PSM state persistence
     */
    void setToIdle();

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

    /**
     * Get current controller state as string
     * @return Current state name (e.g., "IDLE", "PH_MEASURING", "SHUTDOWN")
     */
    std::string getStateAsString() const;

    /**
     * Get status logger instance
     * @return Pointer to the owned CentralStatusLogger
     */
    CentralStatusLogger* getStatusLogger() { return &status_logger_; }

    /**
     * Configure status logger behavior
     * Called from Python during component initialization
     * @param enableReports Enable/disable periodic status reports
     * @param reportIntervalMs Report interval in milliseconds
     * @param verboseMode Enable instant verbose logging (filters out LED changes)
     * @param enable420Mode Enable 420 easter egg at 4:20 AM/PM
     */
    void configureStatusLogger(bool enableReports, uint32_t reportIntervalMs, bool verboseMode, bool enable420Mode);

    /**
     * Calculate current grow day number (1-120)
     * If grow_start_timestamp is configured and time_source is available:
     *   - Gets current time from NTP
     *   - Calculates days elapsed since start date
     *   - Returns (days_elapsed % 120) + 1
     * Otherwise falls back to calendar_manager's manual day counter
     * @return Current day number (1-120)
     */
    uint8_t getCurrentGrowDay();

private:
    // ========================================================================
    // Dependencies
    // ========================================================================

    plantos_hal::HAL* hal_{nullptr};
    esphome::actuator_safety_gate::ActuatorSafetyGate* safety_gate_{nullptr};
    esphome::persistent_state_manager::PersistentStateManager* psm_{nullptr};
    esphome::ezo_ph_uart::EZOPHUARTComponent* ph_sensor_{nullptr};
    esphome::calendar_manager::CalendarManager* calendar_manager_{nullptr};
    esphome::time::RealTimeClock* time_source_{nullptr};

    // Grow cycle configuration
    int64_t grow_start_timestamp_{0};  // Unix timestamp of day 1 (0 = not configured)

    // Night mode configuration
    bool night_mode_enabled_{false};      // Night mode toggle
    uint8_t night_mode_start_hour_{22};   // Start hour (0-23), default 22:00
    uint8_t night_mode_end_hour_{8};      // End hour (0-23), default 08:00

    // Status logger (owned by controller, not injected)
    CentralStatusLogger status_logger_;

    // ========================================================================
    // FSM State Management
    // ========================================================================

    ControllerState current_state_{ControllerState::INIT};
    uint32_t state_start_time_{0};        // millis() when state started
    uint32_t state_counter_{0};           // General-purpose counter for states

    // Boot recovery: State to restore after INIT completes (if PSM had persisted state)
    ControllerState boot_restore_state_{ControllerState::IDLE};
    bool boot_restore_pending_{false};

    // ========================================================================
    // Periodic pH Monitoring State
    // ========================================================================

    uint32_t last_ph_check_time_{0};     // Last time we checked pH (millis())

    // ========================================================================
    // Air Pump Health Monitoring State (IDLE state cycling)
    // ========================================================================

    uint32_t last_air_pump_check_time_{0};  // Last time we verified air pump state (millis())

    // ========================================================================
    // Feed Operation State (Fill + Nutrients + pH)
    // ========================================================================

    bool auto_ph_correction_pending_{false};  // Flag: trigger pH correction after current operation

    // ========================================================================
    // pH Correction State
    // ========================================================================

    float ph_current_{0.0f};              // Current pH reading
    std::vector<float> ph_readings_;      // Buffer for robust averaging
    uint32_t ph_attempt_count_{0};        // Number of pH correction attempts
    float ph_dose_ml_{0.0f};              // Calculated acid dose in milliliters
    uint32_t ph_dose_duration_ms_{0};     // Calculated acid dose duration (converted from mL)
    static constexpr uint8_t MAX_PH_ATTEMPTS = 5;

    // ========================================================================
    // pH Calibration State - Enhanced with robust averaging
    // ========================================================================
    // Each calibration point requires stable readings before proceeding
    // Stability check: 5 batches × 20 readings (1/s), wait 30s between batches
    // Then verify last 3 batches are within 0.1 pH difference

    enum class CalibrationStep {
        IDLE,              // Not calibrating

        // Mid-point calibration (pH 7.00)
        MID_PROMPT,        // Prompt user to insert probe in pH 7.00 buffer
        MID_STABILIZING,   // Taking readings and checking for stability
        MID_CALIBRATE,     // Stable - send calibration command
        MID_COMPLETE,      // Mid-point complete

        // Low-point calibration (pH 4.00)
        LOW_PROMPT,        // Prompt user to insert probe in pH 4.00 buffer
        LOW_STABILIZING,   // Taking readings and checking for stability
        LOW_CALIBRATE,     // Stable - send calibration command
        LOW_COMPLETE,      // Low-point complete

        // High-point calibration (pH 10.01)
        HIGH_PROMPT,       // Prompt user to insert probe in pH 10.01 buffer
        HIGH_STABILIZING,  // Taking readings and checking for stability
        HIGH_CALIBRATE,    // Stable - send calibration command
        HIGH_COMPLETE,     // High-point complete

        COMPLETE           // All calibration points complete
    };

    CalibrationStep calib_step_{CalibrationStep::IDLE};
    uint32_t calib_step_start_time_{0};          // Time when calibration step started
    static constexpr uint32_t CALIB_PROMPT_DURATION = 10000;  // 10 seconds to show prompt

    // Robust averaging for calibration stability check
    static constexpr size_t CALIB_READINGS_PER_BATCH = 20;  // 20 readings per batch
    static constexpr size_t CALIB_TOTAL_BATCHES = 5;        // 5 batches total
    static constexpr uint32_t CALIB_READING_INTERVAL = 1000; // 1 second between readings
    static constexpr uint32_t CALIB_BATCH_WAIT = 30000;      // 30 seconds between batches
    static constexpr float CALIB_STABILITY_THRESHOLD = 0.1f; // ±0.1 pH difference

    float calib_batch_averages_[CALIB_TOTAL_BATCHES];       // Average of each batch
    size_t calib_current_batch_{0};                         // Current batch index
    size_t calib_readings_in_batch_{0};                     // Readings collected in current batch
    float calib_batch_sum_{0.0f};                           // Sum of readings in current batch
    uint32_t calib_last_reading_time_{0};                   // Time of last reading
    uint32_t calib_batch_complete_time_{0};                 // Time when batch completed

    // ========================================================================
    // LED Behavior System
    // ========================================================================

    std::unique_ptr<LedBehaviorSystem> led_behaviors_;

    // ========================================================================
    // State Handlers (called from loop())
    // ========================================================================

    void handleInit();
    void handleIdle();
    void handleNight();
    void handleShutdown();
    void handlePause();
    void handleError();
    void handlePhProcessing();
    void handlePhMeasuring();
    void handlePhCalculating();
    void handlePhInjecting();
    void handlePhMixing();
    void handlePhCalibrating();
    void handleFeeding();
    void handleWaterFilling();
    void handleWaterEmptying();
    void handleFeedFilling();

    // Helper methods for calibration
    void resetCalibrationBatch();
    bool checkCalibrationStability();

    /**
     * Send current water temperature compensation to pH sensor
     * Called before critical pH readings (measuring, calibration, feeding)
     */
    void sendTemperatureCompensation();

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
     * @param forceExecute If true, bypass debouncing (for health monitoring)
     * @return true if approved by SafetyGate
     */
    bool requestPump(const std::string& pumpId, bool state, uint32_t durationSec = 0, bool forceExecute = false);

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
    // pH Correction Helpers
    // ========================================================================

    /**
     * Calculate required acid pump duration based on pH difference
     * @param current_ph Current pH reading
     * @param target_ph_max Upper bound of target pH range
     * @return Dosing duration in milliseconds
     */
    float calculateAcidDoseML(float current_ph, float target_ph_max);

    /**
     * Perform robust average of pH readings
     * Calculates average after rejecting outliers
     * @return Robust average pH value
     */
    float calculateRobustPhAverage();

    /**
     * Check if pH reading is stable (consecutive readings within threshold)
     * @return true if pH has stabilized
     */
    bool isPhStable();

    // ========================================================================
    // Constants
    // ========================================================================

    static constexpr const char* TAG = "plantos_controller";

    // State timeouts (milliseconds)
    static constexpr uint32_t INIT_DURATION = 3000;           // 3 seconds boot
    static constexpr uint32_t ERROR_DURATION = 5000;          // 5 seconds error display
    static constexpr uint32_t PH_MEASURING_DURATION = 30000;  // 30 seconds stabilization
    static constexpr uint32_t PH_MIXING_DURATION = 120000;    // 2 minutes mixing

    // Air pump health monitoring (milliseconds)
    static constexpr uint32_t AIR_PUMP_HEALTH_CHECK_INTERVAL = 30000;  // 30 seconds between checks

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
