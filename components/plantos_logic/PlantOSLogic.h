#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "../actuator_safety_gate/ActuatorSafetyGate.h"
#include "../persistent_state_manager/persistent_state_manager.h"
#include "../controller/CentralStatusLogger.h"
#include "../calendar_manager/CalendarManager.h"

namespace esphome {
namespace plantos_logic {

/**
 * LogicStatus - Enum representing the current state of the PlantOS logic FSM
 *
 * This FSM manages application-level routines (pH correction, feeding, water management)
 * and is separate from the hardware-level FSM in the Controller component.
 */
enum class LogicStatus {
    IDLE,                       // System is stable, waiting for trigger
    PH_CORRECTION_DUE,          // pH sequence triggered, waiting for measurement
    PH_MEASURING,               // All pumps OFF, reading pH (stabilization)
    PH_CALCULATING,             // Determining if injection needed
    PH_INJECTING,               // Acid pump ON (via SafetyGate)
    PH_MIXING,                  // Air pump ON for mixing
    PH_CALIBRATING,             // pH calibration routine active
    FEEDING_DUE,                // Nutrient dosing sequence triggered
    FEEDING_INJECTING,          // Nutrient pumps ON (via SafetyGate)
    WATER_MANAGEMENT_DUE,       // Water change/top-off needed
    WATER_FILLING,              // Freshwater valve ON
    WATER_EMPTYING,             // Wastewater pump ON
    AWAITING_SHUTDOWN           // System in persistent maintenance mode, all routines suppressed
};

/**
 * PlantOSLogic - Main application logic FSM for routine orchestration
 *
 * ============================================================================
 * PURPOSE
 * ============================================================================
 *
 * This component implements the core application logic for PlantOS, managing
 * slow, application-specific flows (dosing, mixing, waiting). It orchestrates:
 * - pH correction sequences (measure, calculate, dose, mix, repeat)
 * - Nutrient feeding cycles
 * - Water management (filling, emptying)
 * - pH calibration
 *
 * This FSM is SEPARATE from the hardware-level FSM in the Controller component.
 * The Controller handles fast hardware states (boot, error, LED patterns).
 * PlantOSLogic handles slow application states (dosing, waiting, mixing).
 *
 * ============================================================================
 * KEY FEATURES
 * ============================================================================
 *
 * 1. SAFETY INTEGRATION: All actuator operations go through ActuatorSafetyGate
 *    to enforce debouncing and duration limits.
 *
 * 2. PERSISTENT STATE: Critical sequence starts/ends logged via PSM for
 *    recovery after power loss.
 *
 * 3. STATUS REPORTING: All state and alert changes reported to CentralStatusLogger.
 *
 * 4. CALENDAR INTEGRATION: Gets target pH and dosing durations from CalendarManager.
 *
 * 5. MANUAL CONTROL: All routines can be manually triggered via public methods
 *    (callable from ESPHome API/buttons).
 *
 * 6. SAFE MODE: When enabled in CalendarManager, automated triggers are disabled
 *    (manual control still works).
 *
 * ============================================================================
 * pH CORRECTION SEQUENCE (Critical Implementation)
 * ============================================================================
 *
 * 1. PH_MEASURING (5 min stabilization):
 *    - Set all pumps OFF via SafetyGate
 *    - Wait 5 minutes (non-blocking)
 *    - Read pH until robust average is stable
 *    -> Transition to PH_CALCULATING
 *
 * 2. PH_CALCULATING (Decision Point):
 *    - Compare pH_current with target range (min/max) from CalendarManager
 *    - Calculate required AcidPumpDuration (ms)
 *    - If pH_current within target OR AcidPumpDuration < 1000ms:
 *      * PSM.clearEvent()
 *      * Transition to IDLE
 *    - Else: Transition to PH_INJECTING
 *
 * 3. PH_INJECTING (Dosing):
 *    - Activate AirPump (Mixing)
 *    - Activate AcidPump via SafetyGate.executeCommand("AcidPump", true, duration_seconds)
 *    - Wait for duration + 200ms safety margin
 *    -> Transition to PH_MIXING
 *
 * 4. PH_MIXING:
 *    - AirPump ON
 *    - Wait 2 minutes
 *    -> Transition back to PH_MEASURING (Max 5 attempts total)
 *
 * 5. ALERT HANDLING:
 *    - If pH_current < 5.0 or > 7.5:
 *      * CentralStatusLogger.updateAlertStatus("PH_CRITICAL", "pH out of safe range")
 *
 * ============================================================================
 * ACTUATOR MAPPING (for SafetyGate)
 * ============================================================================
 *
 * Actuator IDs used in SafetyGate.executeCommand():
 * - "AcidPump"         - pH down dosing pump
 * - "NutrientPumpA"    - Nutrient A dosing pump
 * - "NutrientPumpB"    - Nutrient B dosing pump
 * - "NutrientPumpC"    - Nutrient C dosing pump
 * - "WaterValve"       - Freshwater inlet valve
 * - "WastewaterPump"   - Wastewater outlet pump
 * - "AirPump"          - Mixing air pump
 */
class PlantOSLogic : public Component {
public:
    PlantOSLogic();

    /**
     * ESPHome Component setup() - Initialize FSM and dependencies
     *
     * Called automatically by ESPHome during component initialization.
     */
    void setup() override;

    /**
     * ESPHome Component loop() - FSM driver, executes current state
     *
     * Called continuously by ESPHome's main event loop.
     * Handles non-blocking state transitions and timing.
     */
    void loop() override;

    // ========== Dependency Injection Setters (called from Python) ==========

    void set_safety_gate(actuator_safety_gate::ActuatorSafetyGate* gate) {
        safety_gate_ = gate;
    }

    void set_psm(persistent_state_manager::PersistentStateManager* psm) {
        psm_ = psm;
    }

    void set_status_logger(CentralStatusLogger* logger) {
        status_logger_ = logger;
    }

    void set_calendar(calendar_manager::CalendarManager* calendar) {
        calendar_ = calendar;
    }

    void set_ph_sensor(sensor::Sensor* sensor) {
        ph_sensor_ = sensor;
    }

    void set_state_text(text_sensor::TextSensor* text_sensor) {
        state_text_ = text_sensor;
    }

    // ========== Public Methods (Manual Control API) ==========

    /**
     * Start pH correction sequence
     *
     * Starts the full pH check/correction sequence:
     * IDLE -> PH_CORRECTION_DUE -> PH_MEASURING -> ... -> IDLE
     */
    void start_ph_correction();

    /**
     * Start pH measurement only (no correction)
     *
     * Starts PH_MEASURING only, then returns to IDLE.
     * Useful for checking pH without adjusting it.
     */
    void start_ph_measurement_only();

    /**
     * Start nutrient feeding sequence
     *
     * Starts the FEEDING sequence using dosing durations from CalendarManager.
     */
    void start_feeding();

    /**
     * Start tank filling sequence
     *
     * Starts the WATER_FILLING sequence.
     */
    void start_fill_tank();

    /**
     * Start tank emptying sequence
     *
     * Starts the WATER_EMPTYING sequence.
     */
    void start_empty_tank();

    /**
     * Start pH calibration
     *
     * Calls the pH sensor's calibration method and enters PH_CALIBRATING state.
     * Stops all other routines and timed events.
     */
    void calibrate_ph();

    /**
     * Get current logic status
     *
     * @return Current FSM state
     */
    LogicStatus get_status() const {
        return current_status_;
    }

    /**
     * Get human-readable status string
     *
     * @return String representation of current state
     */
    const char* get_status_string() const;

    /**
     * Toggle maintenance mode (persistent shutdown state)
     *
     * @param state true to enter maintenance mode, false to exit
     * @return Final state of maintenance mode
     *
     * This method enables/disables the persistent maintenance mode that:
     * - Suppresses all automated routines (pH, feeding, water management)
     * - Transitions system to AWAITING_SHUTDOWN state when IDLE
     * - Persists across reboots via PersistentStateManager
     * - Ensures all actuators are turned OFF when entering maintenance
     * - Logs state changes to PSM event log
     */
    bool toggle_maintenance_mode(bool state);

private:
    // ========== Component Dependencies ==========

    actuator_safety_gate::ActuatorSafetyGate* safety_gate_{nullptr};
    persistent_state_manager::PersistentStateManager* psm_{nullptr};
    CentralStatusLogger* status_logger_{nullptr};
    calendar_manager::CalendarManager* calendar_{nullptr};
    sensor::Sensor* ph_sensor_{nullptr};
    text_sensor::TextSensor* state_text_{nullptr};

    // ========== FSM State ==========

    LogicStatus current_status_{LogicStatus::IDLE};
    uint32_t state_start_time_{0};      // Timestamp when state was entered
    uint32_t state_counter_{0};         // General-purpose counter for state logic

    // ========== Maintenance Mode State ==========

    bool shutdown_requested_{false};    // Persistent maintenance mode flag (loaded from PSM)

    // ========== pH Correction State ==========

    float ph_current_{0.0};             // Current pH reading
    std::vector<float> ph_readings_;    // Buffer for robust averaging
    uint32_t ph_attempt_count_{0};      // Number of pH correction attempts
    static constexpr uint8_t MAX_PH_ATTEMPTS = 5;

    // ========== Actuator Names ==========

    static constexpr const char* ACTUATOR_ACID_PUMP = "AcidPump";
    static constexpr const char* ACTUATOR_NUTRIENT_A = "NutrientPumpA";
    static constexpr const char* ACTUATOR_NUTRIENT_B = "NutrientPumpB";
    static constexpr const char* ACTUATOR_NUTRIENT_C = "NutrientPumpC";
    static constexpr const char* ACTUATOR_WATER_VALVE = "WaterValve";
    static constexpr const char* ACTUATOR_WASTEWATER_PUMP = "WastewaterPump";
    static constexpr const char* ACTUATOR_AIR_PUMP = "AirPump";

    // ========== State Handler Functions ==========

    void handle_idle();
    void handle_ph_correction_due();
    void handle_ph_measuring();
    void handle_ph_calculating();
    void handle_ph_injecting();
    void handle_ph_mixing();
    void handle_ph_calibrating();
    void handle_feeding_due();
    void handle_feeding_injecting();
    void handle_water_management_due();
    void handle_water_filling();
    void handle_water_emptying();
    void handle_awaiting_shutdown();

    // ========== Helper Functions ==========

    /**
     * Transition to a new state
     *
     * @param new_status New state to transition to
     */
    void transition_to(LogicStatus new_status);

    /**
     * Turn all pumps OFF via SafetyGate
     */
    void turn_all_pumps_off();

    /**
     * Calculate required acid pump duration based on pH difference
     *
     * @param current_ph Current pH reading
     * @param target_ph_max Upper bound of target pH range
     * @return Dosing duration in milliseconds
     */
    uint32_t calculate_acid_duration(float current_ph, float target_ph_max);

    /**
     * Perform robust average of pH readings
     *
     * Calculates average after rejecting outliers.
     * @return Robust average pH value
     */
    float calculate_robust_ph_average();

    /**
     * Check if pH reading is stable (consecutive readings within threshold)
     *
     * @return true if pH has stabilized
     */
    bool is_ph_stable();

    /**
     * Publish current state to text sensor
     */
    void publish_state();
};

} // namespace plantos_logic
} // namespace esphome
