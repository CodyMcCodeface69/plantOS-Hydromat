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

namespace alert_service {
class AlertService;
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
    FEED_FILLING,      // Fill tank before feeding (part of Feed operation) (Blue Solid)
    EC_PROCESSING,     // Check EC reading, decide if feeding needed (Yellow Pulse)
    EC_CALCULATING,    // Calculate nutrient doses from EC delta (Yellow Fast Blink)
    EC_FEEDING,        // Sequential nutrient pump A → B → C (Orange Pulse)
    EC_MIXING,         // Air pump mixing after EC nutrient dosing (Blue Pulse)
    EC_MEASURING       // Re-measure EC, update K-factor (Yellow Pulse)
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
     * Set Alert Service
     * OPTIONAL - Used for dispatching alerts to configured backends (log, Telegram, etc.)
     */
    void setAlertService(alert_service::AlertService* service) {
        alert_service_ = service;
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

    /**
     * Enable/disable grow light schedule control
     * When disabled, the controller will not automatically control the grow light
     * based on the calendar schedule. Use Shelly scripts for direct control instead.
     * @param enabled true to enable schedule control, false to disable (default: false)
     */
    void setGrowLightScheduleEnabled(bool enabled) {
        grow_light_schedule_enabled_ = enabled;
        ESP_LOGI("controller", "Grow light schedule control %s", enabled ? "ENABLED" : "DISABLED");
    }

    /**
     * Check if grow light schedule control is enabled
     * @return true if schedule control is enabled
     */
    bool isGrowLightScheduleEnabled() const { return grow_light_schedule_enabled_; }

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
     * Start EC check sequence
     * Transitions to EC_PROCESSING state which decides whether feeding is needed
     */
    void startEcCheck();

    /**
     * Enable or disable vacation mode
     * When active: doses reduced to 70%, retry limits increased to 5
     * NVS-persistent across power cycles
     * @param enabled true to enable vacation mode
     */
    void setVacationMode(bool enabled);

    /**
     * Check if vacation mode is active
     * @return true if vacation mode is enabled
     */
    bool isVacationModeEnabled() const { return vacation_mode_; }

    /**
     * Enable or disable automatic water fill
     * When enabled, fill triggers automatically when EMPTY sensor activates in IDLE state
     * NVS-persistent
     * @param enabled true to enable auto-fill
     */
    void setAutoFillEnabled(bool enabled);

    /**
     * Check if automatic water fill is enabled
     * @return true if auto-fill is enabled
     */
    bool isAutoFillEnabled() const { return auto_fill_enabled_; }

    /**
     * Set global dose multiplier override (runtime, not persisted)
     * Applied multiplicatively on top of vacation mode multiplier
     * @param multiplier Scaling factor (1.0 = no override, 0.5 = 50%)
     */
    void setOverrideDoseMultiplier(float multiplier) {
        override_dose_multiplier_ = std::max(0.1f, std::min(2.0f, multiplier));
        ESP_LOGI("controller", "Override dose multiplier set to %.2f", override_dose_multiplier_);
    }

    /**
     * Get current override dose multiplier
     */
    float getOverrideDoseMultiplier() const { return override_dose_multiplier_; }

    /**
     * Enable or disable automatic feeding
     * When enabled, feeding triggers automatically once per day when water level reaches LOW
     * Conditions: IDLE state, not NIGHT/SHUTDOWN/PAUSE, sensors show LOW level (HIGH=OFF, LOW=OFF, EMPTY=ON)
     * @param enabled true to enable auto-feeding, false to disable
     */
    void setAutoFeedingEnabled(bool enabled);

    /**
     * Check if automatic feeding is enabled
     * @return true if auto-feeding is enabled
     */
    bool isAutoFeedingEnabled() const { return auto_feeding_enabled_; }

    // ========================================================================
    // Auto Reservoir Change API (Weekly)
    // ========================================================================

    /**
     * Enable or disable automatic weekly reservoir change
     * When enabled, reservoir change triggers automatically once per week on configured day
     * @param enabled true to enable auto reservoir change, false to disable
     */
    void setAutoReservoirChangeEnabled(bool enabled);

    /**
     * Check if automatic reservoir change is enabled
     * @return true if auto reservoir change is enabled
     */
    bool isAutoReservoirChangeEnabled() const { return auto_reservoir_change_enabled_; }

    /**
     * Set day of week for automatic reservoir change
     * @param day Day of week (0=Sunday, 1=Monday, ..., 6=Saturday)
     */
    void setAutoReservoirChangeDay(uint8_t day);

    /**
     * Get day of week for automatic reservoir change
     * @return Day of week (0=Sunday, 1=Monday, ..., 6=Saturday)
     */
    uint8_t getAutoReservoirChangeDay() const { return auto_reservoir_change_day_; }

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
     * Activate air pump once (for WiFi on_connect)
     * Sends a single command to turn on the air pump
     * Does not set up periodic health checks - just a one-time activation
     */
    void activateAirPump();

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
    alert_service::AlertService* alert_service_{nullptr};

    // Grow cycle configuration
    int64_t grow_start_timestamp_{0};  // Unix timestamp of day 1 (0 = not configured)

    // Night mode configuration
    bool night_mode_enabled_{false};      // Night mode toggle
    uint8_t night_mode_start_hour_{22};   // Start hour (0-23), default 22:00
    uint8_t night_mode_end_hour_{8};      // End hour (0-23), default 08:00

    // Grow light schedule control (default: disabled - use Shelly scripts instead)
    bool grow_light_schedule_enabled_{false};

    // Status logger (owned by controller, not injected)
    CentralStatusLogger status_logger_;

    // ========================================================================
    // FSM State Management
    // ========================================================================

    ControllerState current_state_{ControllerState::INIT};
    uint32_t state_start_time_{0};        // millis() when state started
    uint32_t state_counter_{0};           // General-purpose counter for states
    bool state_entry_executed_{false};    // Prevents duplicate execution of state entry code

    // Boot recovery: State to restore after INIT completes (if PSM had persisted state)
    ControllerState boot_restore_state_{ControllerState::IDLE};
    bool boot_restore_pending_{false};

    // Guards one-time boot actions (sensor check, cooldown init, reboot alert)
    bool boot_alert_sent_{false};

    // ========================================================================
    // Periodic pH Monitoring State
    // ========================================================================

    int64_t last_ph_check_timestamp_{0};  // Last timestamp when pH check was triggered (Unix seconds)
    uint32_t last_ph_check_time_{0};      // Fallback: Last time we checked pH (millis()) if no time source

    // ========================================================================
    // Shelly Health Check State
    // ========================================================================

    uint32_t last_shelly_check_time_{0};              // Last time we checked Shelly health (millis())
    static constexpr uint32_t SHELLY_CHECK_INTERVAL = 30000;  // 30 seconds between checks

    // ========================================================================
    // Sensor Change Flags (ISR-safe)
    // ========================================================================
    // These flags are set by sensor callbacks which may run in ISR context.
    // Using volatile to ensure proper memory synchronization.

    volatile bool temperature_changed_{false};  // Set by ISR callback, cleared by loop()
    float last_temperature_{0.0f};              // Last temperature value received

    // ========================================================================
    // Feed Operation State (Fill + Nutrients + pH)
    // ========================================================================

    bool auto_ph_correction_pending_{false};  // Flag: trigger pH correction after current operation

    // ========================================================================
    // Automatic Feeding State
    // ========================================================================

    /// Enable/disable automatic feeding (persisted to NVS)
    bool auto_feeding_enabled_{true};

    /// Last date when auto-feeding was triggered (Unix timestamp at midnight UTC)
    int64_t last_auto_feed_date_{0};

    /// NVS key for storing last auto-feed date
    static constexpr const char* NVS_KEY_AUTO_FEED_DATE = "AutoFeedDate";

    /// NVS key for auto-feeding enable/disable state
    static constexpr const char* NVS_KEY_AUTO_FEED_ENABLE = "AutoFeedEnable";

    // ========================================================================
    // Operation Context (for nutrient dosing volume selection)
    // ========================================================================

    /// True when running reservoir change sequence, false for normal daily feeding
    /// When true: use getTotalTankVolume() for nutrient dosing
    /// When false: use getTankVolumeDelta() for nutrient dosing
    bool is_reservoir_change_{false};

    // ========================================================================
    // Auto Reservoir Change (Weekly)
    // ========================================================================

    /// Enable/disable automatic weekly reservoir change
    bool auto_reservoir_change_enabled_{false};

    /// Day of week for auto reservoir change (0=Sunday, 1=Monday, ..., 6=Saturday)
    uint8_t auto_reservoir_change_day_{0};

    /// Week number of last auto reservoir change (weeks since Unix epoch)
    int64_t last_auto_reservoir_change_week_{0};

    /// NVS key for auto reservoir change enable/disable state
    static constexpr const char* NVS_KEY_AUTO_RES_ENABLE = "AutoResEnable";

    /// NVS key for auto reservoir change day of week
    static constexpr const char* NVS_KEY_AUTO_RES_DAY = "AutoResDay";

    // ========================================================================
    // Auto-Fill State (Phase 6)
    // ========================================================================

    /// Enable/disable automatic water fill when EMPTY sensor triggers
    bool auto_fill_enabled_{false};

    /// NVS key for auto-fill enable/disable state
    static constexpr const char* NVS_KEY_AUTO_FILL_ENABLE = "AutoFillEn";

    // ========================================================================
    // Vacation Mode (Phase 6)
    // ========================================================================

    bool vacation_mode_{false};

    static constexpr float VACATION_DOSE_MULTIPLIER = 0.70f;  // 70% of normal dose
    static constexpr uint8_t VACATION_MAX_RETRIES = 5;        // 5 retries vs. 3 normal
    static constexpr const char* NVS_KEY_VACATION = "VacMode";

    // ========================================================================
    // Runtime Dose Override (Phase 6)
    // ========================================================================

    float override_dose_multiplier_{1.0f};  // Global dose scaling (1.0 = no override)

    // ========================================================================
    // EC Feeding State
    // ========================================================================

    // EC Adaptive K-factor (EC_Feeding_Logik.md Section 6)
    float ec_K_feed_{0.15f};                        // Current EC K-factor (EMA-smoothed), mL/L per mS/cm
    static constexpr float EC_K_EMA_ALPHA = 0.20f;
    static constexpr float EC_K_MIN_CLAMP = 0.02f;
    static constexpr float EC_K_MAX_CLAMP = 0.50f;
    float ec_pre_feeding_{0.0f};                    // EC value before feeding cycle started (mS/cm)
    float ec_total_ml_per_L_{0.0f};                 // Total mL/L dosed in current EC cycle (accumulated)
    uint8_t ec_attempt_count_{0};                   // Number of EC correction attempts in current cycle
    static constexpr uint8_t MAX_EC_ATTEMPTS = 3;
    int64_t last_ec_feeding_timestamp_{0};           // Unix timestamp (s) of last completed EC feeding
    static constexpr int64_t EC_MIN_INTERVAL_S = 14400;  // 4 hours minimum between EC feedings
    float ec_dose_A_ml_{0.0f};                      // Calculated dose for NutrientPumpA (mL)
    float ec_dose_B_ml_{0.0f};                      // Calculated dose for NutrientPumpB (mL)
    float ec_dose_C_ml_{0.0f};                      // Calculated dose for NutrientPumpC (mL)
    bool ec_cycle_water_filled_{false};             // Guard: water fill happened during EC cycle
    bool auto_ec_check_pending_{false};             // Flag: trigger EC check after WATER_FILLING
    static constexpr const char* NVS_KEY_EC_K = "EcK";

    // Periodic EC monitoring timer (IDLE → EC_PROCESSING)
    uint32_t last_ec_check_time_{0};               // millis() when last EC check was triggered
    static constexpr uint32_t EC_CHECK_INTERVAL_MS = 7200000;  // 2 hours between EC checks

    // ========================================================================
    // pH Correction State
    // ========================================================================

    float ph_current_{0.0f};              // Current pH reading
    std::vector<float> ph_readings_;      // Buffer for robust averaging
    uint32_t ph_attempt_count_{0};        // Number of pH correction attempts
    float ph_dose_ml_{0.0f};              // Calculated acid dose in milliliters
    uint32_t ph_dose_duration_ms_{0};     // Calculated acid dose duration (converted from mL)
    uint32_t ph_mixing_duration_ms_{0};   // Calculated mixing duration based on tank volume
    static constexpr uint8_t MAX_PH_ATTEMPTS = 5;

    // Adaptive pH K-factor (pH_Regellogik.md Section 4)
    float ph_K_{0.07f};                         // Current K-factor (EMA-smoothed)
    static constexpr float PH_K_EMA_ALPHA = 0.20f;
    static constexpr float PH_K_MIN_CLAMP = 0.01f;
    static constexpr float PH_K_MAX_CLAMP = 0.50f;
    static constexpr float PH_CORRECTION_TARGET = 5.85f;  // Biological target pH for injection
    static constexpr float PH_MIN_DOSE_ML = 0.5f;
    static constexpr float PH_MAX_DOSE_ML = 5.0f;
    float ph_cycle_start_ph_{0.0f};             // pH before first injection in cycle
    float ph_cycle_total_ml_{0.0f};             // Total mL dosed in correction cycle
    bool ph_cycle_water_filled_{false};         // Guard: water fill happened during cycle
    bool ph_cycle_aborted_{false};              // Guard: cycle was aborted
    bool ph_post_fill_stabilize_{false};        // Use POST_FILL_STABILIZE_MS in PH_MEASURING (after water fill)
    bool ph_post_feed_stabilize_{false};        // Use POST_FEED_STABILIZE_MS in PH_MEASURING (after EC feeding)
    static constexpr const char* NVS_KEY_PH_K = "PhK";

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
    void handleEcProcessing();
    void handleEcCalculating();
    void handleEcFeeding();
    void handleEcMixing();
    void handleEcMeasuring();

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

    /**
     * Calculate pH mixing duration based on tank volume
     * Linear formula: 1.7L→30s, 70L→120s
     * Returns duration in milliseconds
     */
    uint32_t calculatePhMixingDuration() const;

    /**
     * Check if automatic feeding should trigger
     * Checks all conditions: water level, state, mode, daily limit
     * @return true if all conditions met for auto-feeding
     */
    bool shouldTriggerAutoFeeding();

    /**
     * Get current date as Unix timestamp (midnight UTC)
     * Used for daily feeding limit tracking
     * @return Unix timestamp at start of current day, or 0 if NTP unavailable
     */
    int64_t getCurrentDateTimestamp();

    /**
     * Check if auto reservoir change should be triggered
     * Checks all conditions: enabled, day of week, weekly limit
     * @return true if all conditions met for auto reservoir change
     */
    bool shouldTriggerAutoReservoirChange();

    /**
     * Get current week number (weeks since Unix epoch)
     * Used for weekly reservoir change limit tracking
     * @return Week number, or 0 if NTP unavailable
     */
    int64_t getCurrentWeekNumber();

    /**
     * Get current day of week
     * @return Day of week (0=Sunday, 1=Monday, ..., 6=Saturday), or 255 if unavailable
     */
    uint8_t getCurrentDayOfWeek();

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

    /**
     * Update adaptive pH K-factor after a completed correction cycle
     * Uses EMA smoothing and saves result to NVS
     * @param ph_before pH at start of cycle (before first injection)
     * @param ph_after pH after final measurement
     * @param ml_total Total mL of acid dosed in cycle
     */
    void updatePhKFactor(float ph_before, float ph_after, float ml_total);

    /**
     * Update adaptive EC K-factor after a completed feeding cycle
     * @param ec_before EC at start of cycle (mS/cm)
     * @param ec_after EC after final measurement (mS/cm)
     * @param ml_per_L_total Total mL/L of nutrients dosed across all attempts
     */
    void updateEcKFactor(float ec_before, float ec_after, float ml_per_L_total);

    /**
     * Transition to PH_PROCESSING if auto_ph_correction_pending_, otherwise IDLE.
     * Used by EC_PROCESSING when it exits without feeding (EC OK, EC unconfigured, etc.)
     * Implements post-fill sequencing: water fill → EC check → pH correction
     */
    void transitionAfterEcSkipped();

    // ========================================================================
    // Constants
    // ========================================================================

    static constexpr const char* TAG = "plantos_controller";

    // State timeouts (milliseconds)
    static constexpr uint32_t INIT_DURATION = 3000;           // 3 seconds boot
    static constexpr uint32_t ERROR_DURATION = 5000;          // 5 seconds error display
    static constexpr uint32_t PH_MEASURING_DURATION = 30000;  // 30 seconds stabilization (normal)
    static constexpr uint32_t POST_FILL_STABILIZE_MS = 600000; // 10 min - pH stabilization after water fill
    static constexpr uint32_t POST_FEED_STABILIZE_MS = 300000; // 5 min - pH stabilization after EC feeding
    // NOTE: PH_MIXING_DURATION is now calculated dynamically based on tank volume
    // See calculatePhMixingDuration() for linear formula (1.7L→30s, 70L→120s)

    // pH correction parameters
    static constexpr float PH_TARGET_MIN = 5.5f;
    static constexpr float PH_TARGET_MAX = 6.5f;
    static constexpr uint32_t PH_MAX_ITERATIONS = 5;

    // ========================================================================
    // ENHANCED ERROR HANDLING & RETRY LOGIC
    // ========================================================================

    /**
     * OperationRetryState - Tracks retry attempts for current operation
     *
     * This structure is reset at the start of each major operation (pH correction,
     * feeding, water operations) and tracks retry attempts during that operation.
     */
    struct OperationRetryState {
        std::string operation_name;      // "pH_CORRECTION", "FEEDING_PUMP_A", etc.
        uint8_t retry_count;             // Current retry attempt (0 = first try)
        uint8_t max_retries;             // Maximum retries allowed
        uint32_t last_retry_time;        // millis() of last retry attempt
        uint32_t backoff_delay_ms;       // Current backoff delay

        // Track what completed successfully for context
        std::vector<std::string> completed_steps;  // e.g., ["NutrientPumpA", "NutrientPumpB"]

        OperationRetryState()
            : operation_name(""), retry_count(0), max_retries(3),
              last_retry_time(0), backoff_delay_ms(0) {}

        void reset(const std::string& op_name, uint8_t max_retry = 3) {
            operation_name = op_name;
            retry_count = 0;
            max_retries = max_retry;
            last_retry_time = 0;
            backoff_delay_ms = 1000;  // Start with 1 second
            completed_steps.clear();
        }

        bool canRetry() const {
            return retry_count < max_retries;
        }

        void incrementRetry() {
            retry_count++;
            last_retry_time = esphome::millis();
            // Exponential backoff: 1s, 2s, 4s, 8s...
            backoff_delay_ms = std::min(static_cast<uint32_t>(backoff_delay_ms * 2), static_cast<uint32_t>(10000));  // Cap at 10s
        }

        bool isBackoffComplete(uint32_t current_time) const {
            if (retry_count == 0) return true;  // First try, no backoff
            return (current_time - last_retry_time) >= backoff_delay_ms;
        }

        std::string getContextString() const {
            std::string context = operation_name;
            context += " (attempt " + std::to_string(retry_count + 1) + "/" + std::to_string(max_retries + 1) + ")";
            if (!completed_steps.empty()) {
                context += ", completed: [";
                for (size_t i = 0; i < completed_steps.size(); i++) {
                    if (i > 0) context += ", ";
                    context += completed_steps[i];
                }
                context += "]";
            }
            return context;
        }
    };

    // Current retry state for active operation
    OperationRetryState retry_state_;

    /**
     * SensorRetryState - Tracks consecutive sensor read failures
     *
     * Separate from operation retry to allow faster retry cycles for sensor failures.
     */
    struct SensorRetryState {
        uint8_t consecutive_failures;    // Consecutive sensor read failures
        uint32_t last_failure_time;      // millis() of last failure
        uint32_t backoff_delay_ms;       // Current backoff delay
        static constexpr uint8_t MAX_SENSOR_RETRIES = 5;

        SensorRetryState()
            : consecutive_failures(0), last_failure_time(0), backoff_delay_ms(5000) {}

        void recordFailure() {
            consecutive_failures++;
            last_failure_time = esphome::millis();
            // Exponential backoff: 5s, 10s, 15s
            backoff_delay_ms = std::min(static_cast<uint32_t>(5000 + (consecutive_failures * 5000)), static_cast<uint32_t>(15000));
        }

        void reset() {
            consecutive_failures = 0;
            last_failure_time = 0;
            backoff_delay_ms = 5000;
        }

        bool shouldRetry() const {
            return consecutive_failures < MAX_SENSOR_RETRIES;
        }

        bool isBackoffComplete(uint32_t current_time) const {
            if (consecutive_failures == 0) return true;
            return (current_time - last_failure_time) >= backoff_delay_ms;
        }
    };

    SensorRetryState sensor_retry_state_;

    // ========================================================================
    // SENSOR HEALTH - 3-Tier Model (Phase 7)
    // ========================================================================

    /**
     * SensorTier - Three-tier degradation model for ambient sensor health
     *
     * OK       → sensor reading is valid
     * STOPPED  → first failure detected, operations suspended for this sensor
     * RETRYING → retry attempts in progress (with 60s interval)
     * FAILED   → max retries exhausted, alert remains until manual recovery
     */
    enum class SensorTier { OK, STOPPED, RETRYING, FAILED };

    struct SensorHealth {
        SensorTier tier{SensorTier::OK};
        uint8_t retry_count{0};
        uint8_t max_retries{3};
        uint32_t last_retry_time{0};
        static constexpr uint32_t RETRY_INTERVAL_MS = 60000;  // 60s between retries

        void recordFailure(uint32_t now) {
            last_retry_time = now;
            if (tier == SensorTier::OK) {
                tier = SensorTier::STOPPED;
                retry_count = 1;
            } else if (tier == SensorTier::STOPPED || tier == SensorTier::RETRYING) {
                retry_count++;
                tier = (retry_count >= max_retries) ? SensorTier::FAILED : SensorTier::RETRYING;
            }
            // FAILED stays FAILED until recordSuccess()
        }

        void recordSuccess() {
            tier = SensorTier::OK;
            retry_count = 0;
            last_retry_time = 0;
        }

        bool canRetry(uint32_t now) const {
            if (tier == SensorTier::FAILED) return false;
            if (retry_count == 0) return true;
            return (now - last_retry_time) >= RETRY_INTERVAL_MS;
        }

        bool isFailed() const { return tier == SensorTier::FAILED; }
        bool isOK() const { return tier == SensorTier::OK; }

        const char* tierName() const {
            switch (tier) {
                case SensorTier::OK:       return "OK";
                case SensorTier::STOPPED:  return "STOPPED";
                case SensorTier::RETRYING: return "RETRYING";
                case SensorTier::FAILED:   return "FAILED";
                default:                   return "UNKNOWN";
            }
        }
    };

    SensorHealth ph_health_;
    SensorHealth ec_health_;
    SensorHealth temp_health_;

    // Sensor plausibility tracking (Phase 7)
    float last_ph_plausibility_value_{0.0f};    // Previous pH for rate-of-change check
    int64_t last_ph_plausibility_time_{0};       // Unix timestamp of last pH plausibility reading
    float last_ec_plausibility_value_{-1.0f};    // Previous EC for jump detection (-1 = unset)

    // Periodic sensor health check timer
    uint32_t last_sensor_health_check_time_{0};
    static constexpr uint32_t SENSOR_HEALTH_CHECK_INTERVAL_MS = 60000;  // 1 min

    // ========================================================================
    // CALIBRATION REMINDERS (Phase 7)
    // ========================================================================

    int64_t last_ph_calibration_ts_{0};   // Unix timestamp of last pH calibration
    int64_t last_ec_calibration_ts_{0};   // Unix timestamp of last EC calibration
    static constexpr int64_t PH_CALIB_INTERVAL_S = 1209600;   // 14 days
    static constexpr int64_t EC_CALIB_INTERVAL_S = 2592000;   // 30 days
    static constexpr const char* NVS_KEY_PH_CALIB_TS = "PhCalibTs";
    static constexpr const char* NVS_KEY_EC_CALIB_TS = "EcCalibTs";

    // Compile-time feature flag for enhanced error handling
#ifdef PLANTOS_ENHANCED_ERROR_HANDLING
    static constexpr bool ENHANCED_ERROR_HANDLING_ENABLED = true;
#else
    static constexpr bool ENHANCED_ERROR_HANDLING_ENABLED = false;
#endif

    // Helper methods for enhanced error handling
    bool requestPumpAdaptive(const std::string& pumpId, bool state, uint32_t requested_duration_sec, bool forceExecute);
    void initOperationRetry(const std::string& operation_name, uint8_t max_retries);
    bool canRetryOperation();
    void recordOperationStep(const std::string& step_name);
    void retryOperation();
    void checkHardwareStatus();

    /**
     * Check Shelly device health via HTTP ping
     * Updates CentralStatusLogger with device status
     * Called periodically from IDLE state (every 30 seconds)
     */
    void checkShellyHealth();

    /**
     * Check water temperature against safe thresholds and fire/clear alerts
     * Thresholds: <15°C WARNING, >28°C WARNING, >32°C CRITICAL
     */
    void checkTemperatureAlerts();

    /**
     * Check pH and EC readings for out-of-range values and suspicious changes
     * pH: valid range 3.0–10.0, rate-of-change limit 1.0 pH/min
     * EC: valid range 0–5.0 mS/cm, jump limit 1.0 mS/cm between checks
     */
    void checkSensorPlausibility();

    /**
     * Check water level sensor states for logical contradictions
     * HIGH=true + EMPTY=true simultaneously is physically impossible
     */
    void checkWaterLevelPlausibility();

    /**
     * Check if pH or EC calibration is overdue and fire reminder alerts
     * pH: 14-day interval, EC: 30-day interval
     * Only runs when NTP time is available
     */
    void checkCalibrationReminders();

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
