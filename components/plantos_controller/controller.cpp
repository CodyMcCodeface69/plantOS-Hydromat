#include "controller.h"
#include "esphome/components/plantos_hal/hal.h"
#include "esphome/components/actuator_safety_gate/ActuatorSafetyGate.h"
#include "esphome/components/persistent_state_manager/persistent_state_manager.h"
#include "esphome/components/calendar_manager/CalendarManager.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/alert_service/alert_service.h"
#include <algorithm> // for std::sort, std::min, std::max
#include <cmath>    // for std::sqrt

/*
╔════════════════════════════════════════════════════════════════════════════════════════╗
║                                                                                        ║
║                    PlantOS Controller - Unified FSM Implementation                     ║
║                                                                                        ║
║  IMPORTANT: For complete FSM documentation including state diagrams, transitions,     ║
║             triggers, and implementation guidelines, see:                              ║
║                                                                                        ║
║             /home/cody/plantOS-testlab/FSMINFO.md                                      ║
║                                                                                        ║
║  This file contains the implementation of all state handlers and transitions.          ║
║  FSMINFO.md is the authoritative source for FSM behavior and MUST be updated          ║
║  when making changes to states, transitions, actuator actions, or timeouts.           ║
║                                                                                        ║
╚════════════════════════════════════════════════════════════════════════════════════════╝
*/

namespace plantos_controller {

PlantOSController::PlantOSController() {
    // Initialize LED behavior system
    led_behaviors_ = std::make_unique<LedBehaviorSystem>();
}

// ============================================================================
// ESPHome Component Lifecycle
// ============================================================================

void PlantOSController::setup() {
    ESP_LOGI(TAG, "PlantOS Unified Controller initializing...");

    // Initialize status logger
    status_logger_.begin();

    // Verify critical dependencies
    if (!hal_) {
        ESP_LOGE(TAG, "HAL not configured - Controller cannot function!");
        return;
    }

    if (!safety_gate_) {
        ESP_LOGW(TAG, "SafetyGate not configured - Actuator control disabled");
    }

    if (psm_) {
        ESP_LOGI(TAG, "PSM configured - State persistence enabled");

        // Load auto-feeding state from NVS
        auto_feeding_enabled_ = psm_->loadState(NVS_KEY_AUTO_FEED_ENABLE, true);
        ESP_LOGI(TAG, "Auto-feeding: %s", auto_feeding_enabled_ ? "ENABLED" : "DISABLED");

        // Load auto-fill state from NVS (default OFF - safer)
        auto_fill_enabled_ = psm_->loadState(NVS_KEY_AUTO_FILL_ENABLE, false);
        ESP_LOGI(TAG, "Auto-fill: %s", auto_fill_enabled_ ? "ENABLED" : "DISABLED");

        // Load auto pH correction state from NVS (default ON)
        auto_ph_correction_enabled_ = psm_->loadState(NVS_KEY_AUTO_PH_CORR, true);
        ESP_LOGI(TAG, "Auto pH correction: %s", auto_ph_correction_enabled_ ? "ENABLED" : "DISABLED");

        // Load adaptive pH K-factor from NVS
        ph_K_ = psm_->loadFloat(NVS_KEY_PH_K, 0.07f);
        ESP_LOGI(TAG, "pH K-factor loaded from NVS: %.4f", ph_K_);

        // Load adaptive EC K-factor from NVS
        ec_K_feed_ = psm_->loadFloat(NVS_KEY_EC_K, 0.00015f);
        ESP_LOGI(TAG, "EC K-factor loaded from NVS: %.4f", ec_K_feed_);

        // Initialize last feed date (will be loaded from NVS on first check)
        last_auto_feed_date_ = 0;

        // Load calibration timestamps from NVS (Phase 7)
        last_ph_calibration_ts_ = (int64_t)psm_->loadInt32(NVS_KEY_PH_CALIB_TS, 0);
        last_ec_calibration_ts_ = (int64_t)psm_->loadInt32(NVS_KEY_EC_CALIB_TS, 0);
        ESP_LOGI(TAG, "pH last calibrated: %lld s, EC last calibrated: %lld s",
                 (long long)last_ph_calibration_ts_, (long long)last_ec_calibration_ts_);
    } else {
        ESP_LOGW(TAG, "PSM not configured - State persistence disabled");
    }

    // Register temperature change callback (ISR-safe - no logging in callback!)
    // Temperature sensor callbacks can be triggered from ISR context (ADC interrupts),
    // so we must NOT call ESP_LOGI or any FreeRTOS queue operations here.
    // Instead, we set a volatile flag and handle logging in the main loop.
    if (hal_->hasTemperature()) {
        hal_->onTemperatureChange([this](float temp) {
            // ISR-SAFE: Only update volatile members, no logging!
            last_temperature_ = temp;
            temperature_changed_ = true;
        });
        ESP_LOGI(TAG, "Temperature sensor callback registered (ISR-safe)");
    } else {
        ESP_LOGW(TAG, "Temperature sensor not available - temperature compensation disabled");
    }

    // Initialize periodic pH check timer
    last_ph_check_time_ = esphome::millis();
    if (hal_->hasTime()) {
        last_ph_check_timestamp_ = hal_->getCurrentTimestamp();
        ESP_LOGI(TAG, "Time source available - using clock-based pH scheduling");
    } else {
        last_ph_check_timestamp_ = 0;
        ESP_LOGW(TAG, "No time source - using fallback boot-time based pH scheduling");
    }

    uint32_t ph_interval_hours = hal_->get_ph_reading_interval() / 3600000;  // Convert ms to hours
    uint32_t ph_interval_days = ph_interval_hours / 24;

    if (ph_interval_days > 0) {
        ESP_LOGI(TAG, "Periodic pH monitoring: every %u day%s (%u hours)",
                 ph_interval_days, ph_interval_days > 1 ? "s" : "", ph_interval_hours);
    } else {
        ESP_LOGI(TAG, "Periodic pH monitoring: every %u hours", ph_interval_hours);
    }

    // Always start in INIT state for boot sequence
    ESP_LOGI(TAG, "Controller initialized - Starting INIT boot sequence");
    transitionTo(ControllerState::INIT);
}

void PlantOSController::loop() {
    if (!hal_) {
        return; // Cannot operate without HAL
    }

    // Update LED behavior for current state
    uint32_t elapsed = getStateElapsed();
    led_behaviors_->update(current_state_, elapsed, hal_);

    // Handle temperature change notifications (ISR-safe deferred logging)
    // The callback sets temperature_changed_ flag from ISR context,
    // and we do the actual logging here in the main loop to avoid
    // calling ESP_LOGI from ISR which causes alignment crashes on RISC-V.
    if (temperature_changed_) {
        temperature_changed_ = false;  // Clear flag
        if (status_logger_.isVerboseMode()) {
            ESP_LOGI(TAG, "Temperature changed: %.1f°C", last_temperature_);
        }
    }

    // Check if we should print periodic status report (or one was requested on demand)
    bool forced_report = status_report_requested_;
    if (status_logger_.shouldPrintStatusReport() || forced_report) {
        status_report_requested_ = false;
        // Update status logger with current pH before printing
        if (hal_->hasPhValue()) {
            status_logger_.updateStatus(hal_->readPH(), "");
        }

        // Update UART hardware status (EZO pH sensor)
        if (hal_) {
            std::vector<UARTDeviceInfo> uartDevices;
            std::string status = "";

            // Check if sensor is ready
            bool isReady = hal_->isPhSensorReady();

            // Add calibration status if available
            // Note: Calibration query would require async call, so we just show ready status
            if (isReady) {
                status = "Ready";
            } else {
                status = "Not responding";
            }

            uartDevices.push_back(UARTDeviceInfo(
                "EZO pH Sensor",
                "TX=GPIO18, RX=GPIO19 (uart_bus)",
                isReady,
                true,  // critical
                status
            ));

            status_logger_.updateUARTHardwareStatus(uartDevices);
        }

        // Update 1-Wire hardware status (DS18B20 temperature sensor)
        // Always report if configured, even if waiting for first reading
        std::vector<OneWireDeviceInfo> oneWireDevices;

        bool hasReading = hal_->hasTemperature();
        std::string status;
        float temp = 0.0f;

        if (hasReading) {
            temp = hal_->readTemperature();
            char statusBuf[32];
            snprintf(statusBuf, sizeof(statusBuf), "%.1f°C", temp);
            status = std::string(statusBuf);
        } else {
            status = "Waiting for first reading";
        }

        oneWireDevices.push_back(OneWireDeviceInfo(
            "DS18B20 Temperature",
            "GPIO3",
            hasReading,  // ready when has reading
            true,        // critical for pH compensation
            status
        ));

        status_logger_.updateOneWireHardwareStatus(oneWireDevices);

        // Update water temperature in sensor data section
        status_logger_.updateWaterTemperature(temp, hasReading);

        // Update EC sensor data in status logger
        status_logger_.updateEC(hal_->readEC(), hal_->hasECValue());

        // Update water level sensors in sensor data section (3-sensor system)
        bool hasWaterLevelSensors = hal_->hasWaterLevelSensors();
        bool highSensor = false;
        bool lowSensor = false;
        bool emptySensor = false;

        if (hasWaterLevelSensors) {
            highSensor = hal_->readWaterLevelHigh();
            lowSensor = hal_->readWaterLevelLow();
            emptySensor = hal_->readWaterLevelEmpty();
        }

        status_logger_.updateWaterLevelSensors(highSensor, lowSensor, emptySensor, hasWaterLevelSensors);

        // Update pump configurations in status logger
        std::vector<PumpConfigInfo> pumpConfigs;
        pumpConfigs.push_back(PumpConfigInfo(
            "pH Pump (AcidPump)",
            hal_->getPumpConfig("AcidPump").flow_rate_ml_s,
            hal_->getPumpConfig("AcidPump").pwm_intensity
        ));
        pumpConfigs.push_back(PumpConfigInfo(
            "Grow Pump (NutrientPumpA)",
            hal_->getPumpConfig("NutrientPumpA").flow_rate_ml_s,
            hal_->getPumpConfig("NutrientPumpA").pwm_intensity
        ));
        pumpConfigs.push_back(PumpConfigInfo(
            "Micro Pump (NutrientPumpB)",
            hal_->getPumpConfig("NutrientPumpB").flow_rate_ml_s,
            hal_->getPumpConfig("NutrientPumpB").pwm_intensity
        ));
        pumpConfigs.push_back(PumpConfigInfo(
            "Bloom Pump (NutrientPumpC)",
            hal_->getPumpConfig("NutrientPumpC").flow_rate_ml_s,
            hal_->getPumpConfig("NutrientPumpC").pwm_intensity
        ));
        status_logger_.updatePumpConfigurations(pumpConfigs);

        // Update PSM event info in status logger
        if (psm_) {
            if (psm_->hasEvent()) {
                esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
                status_logger_.updatePSMEvent(event.eventID, event.status, psm_->getEventAge());
            } else {
                // No event - clear PSM info in status logger
                status_logger_.updatePSMEvent("", 0, -1);
            }
        }

        // Update calendar status in status logger
        if (calendar_manager_) {
            // Use calendar manager's actual current day (not timestamp-based calculation)
            uint8_t currentDay = calendar_manager_->get_current_day();
            esphome::calendar_manager::DailySchedule schedule = calendar_manager_->get_schedule(currentDay);
            status_logger_.updateCalendarStatus(
                currentDay,
                schedule.target_ph_min,
                schedule.target_ph_max,
                schedule.nutrient_A_ml_per_liter,
                schedule.nutrient_B_ml_per_liter,
                schedule.nutrient_C_ml_per_liter,
                calendar_manager_->is_safe_mode(),
                schedule.ec_target,
                ec_target_multiplier_
            );
        }

        status_logger_.logStatus(forced_report);

        // Log water level status with ASCII art visualization (3-sensor system)
        if (hal_->hasWaterLevelSensors()) {
            bool high = hal_->readWaterLevelHigh();
            bool low = hal_->readWaterLevelLow();
            bool empty = hal_->readWaterLevelEmpty();
            status_logger_.logWaterLevelStatus(high, low, empty, true);
        } else {
            status_logger_.logWaterLevelStatus(false, false, false, false);  // Sensors offline
        }
    }

    // Call state-specific handler
    switch (current_state_) {
        case ControllerState::INIT:
            handleInit();
            break;

        case ControllerState::IDLE:
            handleIdle();
            break;

        case ControllerState::NIGHT:
            handleNight();
            break;

        case ControllerState::SHUTDOWN:
            handleShutdown();
            break;

        case ControllerState::PAUSE:
            handlePause();
            break;

        case ControllerState::ERROR:
            handleError();
            break;

        case ControllerState::PH_PROCESSING:
            handlePhProcessing();
            break;

        case ControllerState::PH_MEASURING:
            handlePhMeasuring();
            break;

        case ControllerState::PH_CALCULATING:
            handlePhCalculating();
            break;

        case ControllerState::PH_INJECTING:
            handlePhInjecting();
            break;

        case ControllerState::PH_MIXING:
            handlePhMixing();
            break;

        case ControllerState::PH_CALIBRATING:
            handlePhCalibrating();
            break;

        case ControllerState::FEEDING:
            handleFeeding();
            break;

        case ControllerState::WATER_FILLING:
            handleWaterFilling();
            break;

        case ControllerState::WATER_EMPTYING:
            handleWaterEmptying();
            break;

        case ControllerState::FEED_FILLING:
            handleFeedFilling();
            break;

        case ControllerState::EC_PROCESSING:
            handleEcProcessing();
            break;

        case ControllerState::EC_CALCULATING:
            handleEcCalculating();
            break;

        case ControllerState::EC_FEEDING:
            handleEcFeeding();
            break;

        case ControllerState::EC_MIXING:
            handleEcMixing();
            break;

        case ControllerState::EC_MEASURING:
            handleEcMeasuring();
            break;

        case ControllerState::EC_CALIBRATING:
            handleEcCalibrating();
            break;
    }

    // ========================================================================
    // Time-based Light Control
    // ========================================================================
    // Check if we should update grow light based on calendar schedule
    // Only run when:
    // - Grow light schedule is enabled (default: disabled, use Shelly scripts instead)
    // - System is operational (not in SHUTDOWN, PAUSE, or ERROR)
    if (grow_light_schedule_enabled_ && calendar_manager_ && time_source_ &&
        current_state_ != ControllerState::SHUTDOWN &&
        current_state_ != ControllerState::PAUSE &&
        current_state_ != ControllerState::ERROR) {

        // Get current time
        auto now = time_source_->now();
        if (now.is_valid()) {
            // Get today's schedule
            auto schedule = calendar_manager_->get_today_schedule();

            // Calculate current time in minutes since midnight
            uint16_t current_minutes = now.hour * 60 + now.minute;

            // Determine if light should be ON
            // Handle wraparound case (e.g., ON at 16:00, OFF at 08:00 means light is on overnight)
            bool should_be_on = false;
            if (schedule.light_on_time < schedule.light_off_time) {
                // Normal case: ON and OFF within same day (e.g., ON at 08:00, OFF at 16:00)
                should_be_on = (current_minutes >= schedule.light_on_time &&
                               current_minutes < schedule.light_off_time);
            } else {
                // Wraparound case: light is on overnight (e.g., ON at 16:00, OFF at 08:00)
                should_be_on = (current_minutes >= schedule.light_on_time ||
                               current_minutes < schedule.light_off_time);
            }

            // Track previous state to avoid redundant commands
            static bool previous_light_state = false;
            static bool first_run = true;

            // Update light state if changed (or on first run)
            // Uses requestPump to go through ASG for proper tracking
            if (first_run || previous_light_state != should_be_on) {
                if (should_be_on) {
                    ESP_LOGI(TAG, "Grow light schedule: Turning ON (Day %d, Schedule: %02d:%02d ON / %02d:%02d OFF)",
                             schedule.day_number,
                             schedule.light_on_time / 60, schedule.light_on_time % 60,
                             schedule.light_off_time / 60, schedule.light_off_time % 60);
                    requestPump("GrowLight", true, 0);  // 0 = no duration limit
                } else {
                    ESP_LOGI(TAG, "Grow light schedule: Turning OFF (Day %d, Schedule: %02d:%02d ON / %02d:%02d OFF)",
                             schedule.day_number,
                             schedule.light_on_time / 60, schedule.light_on_time % 60,
                             schedule.light_off_time / 60, schedule.light_off_time % 60);
                    requestPump("GrowLight", false);
                }
                previous_light_state = should_be_on;
                first_run = false;
            }
        }
    }
}

// ============================================================================
// State Handlers
// ============================================================================

void PlantOSController::handleInit() {
    // Boot sequence: Red → Yellow → Green (3 seconds)
    // LED behavior handles visual feedback

    uint32_t elapsed = getStateElapsed();

    // On first entry (elapsed < 100ms): Check PSM for persisted states
    // We check here (not in setup()) because PSM might initialize after controller
    if (elapsed < 100 && !boot_restore_pending_) {
        if (psm_) {
            ESP_LOGI(TAG, "Checking PSM for persisted state...");

            // Check if we were in SHUTDOWN or PAUSE state before power loss
            if (psm_->hasEvent()) {
                esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
                std::string eventID(event.eventID);

                // Check if event ID is non-empty (not a cleared event)
                if (!eventID.empty()) {
                    if (eventID == "STATE_SHUTDOWN") {
                        ESP_LOGW(TAG, "RECOVERY: SHUTDOWN state found in PSM (age: %lld sec) - will restore after INIT",
                                 psm_->getEventAge());
                        boot_restore_state_ = ControllerState::SHUTDOWN;
                        boot_restore_pending_ = true;
                    } else if (eventID == "STATE_PAUSE") {
                        ESP_LOGW(TAG, "RECOVERY: PAUSE state found in PSM (age: %lld sec) - will restore after INIT",
                                 psm_->getEventAge());
                        boot_restore_state_ = ControllerState::PAUSE;
                        boot_restore_pending_ = true;
                    } else if (eventID == "WATER_FILLING") {
                        ESP_LOGW(TAG, "RECOVERY: Interrupted WATER_FILLING operation found (age: %lld sec)", psm_->getEventAge());
                        ESP_LOGW(TAG, "RECOVERY: Force-closing water valve to prevent overflow");
                        // Close valve via SafetyGate
                        if (safety_gate_) {
                            safety_gate_->executeCommand(WATER_VALVE, false, 0);
                        }
                        // Clear PSM event
                        psm_->clearEvent();
                        ESP_LOGI(TAG, "RECOVERY: Water valve closed, PSM event cleared");
                    } else if (eventID == "WATER_EMPTYING") {
                        ESP_LOGW(TAG, "RECOVERY: Interrupted WATER_EMPTYING operation found (age: %lld sec)", psm_->getEventAge());
                        ESP_LOGW(TAG, "RECOVERY: Force-stopping wastewater pump");
                        // Stop pump via SafetyGate
                        if (safety_gate_) {
                            safety_gate_->executeCommand(WASTEWATER_PUMP, false, 0);
                        }
                        // Clear PSM event
                        psm_->clearEvent();
                        ESP_LOGI(TAG, "RECOVERY: Wastewater pump stopped, PSM event cleared");
                    } else {
                        // Some other operation was interrupted - log warning
                        ESP_LOGW(TAG, "RECOVERY: Interrupted operation found: %s (status: %d, age: %lld sec)",
                                 eventID.c_str(), event.status, psm_->getEventAge());
                        ESP_LOGW(TAG, "Operation was likely interrupted by power loss - check if recovery needed");
                        if (alert_service_) {
                            alert_service_->alert(alert_service::AlertLevel::WARNING,
                                "Operation interrupted",
                                "Operation '" + eventID + "' was interrupted by power loss (age: " +
                                std::to_string(psm_->getEventAge()) + "s) — verify system state");
                        }
                    }
                } else {
                    ESP_LOGD(TAG, "PSM event is empty (cleared) - no restoration needed");
                }
            } else {
                ESP_LOGI(TAG, "No PSM event found - clean boot");
            }
        }
    }

    // ========================================================================
    // ONE-TIME BOOT ACTIONS (Phase 8 - Enhanced Reboot Recovery)
    // Runs once during INIT, regardless of elapsed time.
    // Cooldowns, sensor check, reboot alert.
    // ========================================================================
    if (!boot_alert_sent_) {
        boot_alert_sent_ = true;
        uint32_t now = hal_->getSystemTime();

        last_sensor_health_check_time_ = now;
        ESP_LOGI(TAG, "BOOT: Cooldowns initialized");

        // Boot sensor check - log all available sensor readings
        ESP_LOGI(TAG, "BOOT SENSOR CHECK:");
        if (hal_->hasPhValue()) {
            ESP_LOGI(TAG, "  pH        : %.2f", hal_->readPH());
        } else {
            ESP_LOGW(TAG, "  pH        : NOT AVAILABLE");
        }
        if (hal_->hasECValue()) {
            ESP_LOGI(TAG, "  EC        : %.1f uS/cm (%.3f mS/cm)", hal_->readEC(), hal_->readEC() / 1000.0f);
        } else {
            ESP_LOGW(TAG, "  EC        : NOT AVAILABLE");
        }
        if (hal_->hasTemperature()) {
            ESP_LOGI(TAG, "  Temp      : %.1f\xC2\xB0""C", hal_->readTemperature());
        } else {
            ESP_LOGW(TAG, "  Temp      : NOT AVAILABLE");
        }
        if (hal_->hasWaterLevelSensors()) {
            ESP_LOGI(TAG, "  WaterLevel: HIGH=%s LOW=%s EMPTY=%s",
                     hal_->readWaterLevelHigh() ? "ON" : "off",
                     hal_->readWaterLevelLow()  ? "ON" : "off",
                     hal_->readWaterLevelEmpty() ? "ON" : "off");
        } else {
            ESP_LOGW(TAG, "  WaterLevel: NOT CONFIGURED");
        }

        // Reboot notification via AlertService
        if (alert_service_) {
            alert_service_->alert(alert_service::AlertLevel::INFO,
                "System rebooted",
                "PlantOS controller boot complete — all subsystems initializing");
        }
    }

    if (elapsed >= INIT_DURATION) {
        ESP_LOGI(TAG, "Boot sequence complete");

        // Check if we need to restore a persisted state from PSM
        if (boot_restore_pending_) {
            boot_restore_pending_ = false;

            if (boot_restore_state_ == ControllerState::SHUTDOWN) {
                ESP_LOGW(TAG, "RECOVERY: Restoring SHUTDOWN state from power loss");
                if (alert_service_) {
                    alert_service_->alert(alert_service::AlertLevel::WARNING,
                        "State restored: SHUTDOWN",
                        "System was in SHUTDOWN before power loss — resuming SHUTDOWN");
                }
                transitionTo(ControllerState::SHUTDOWN);
            } else if (boot_restore_state_ == ControllerState::PAUSE) {
                ESP_LOGW(TAG, "RECOVERY: Restoring PAUSE state from power loss");
                if (alert_service_) {
                    alert_service_->alert(alert_service::AlertLevel::WARNING,
                        "State restored: PAUSE",
                        "System was in PAUSE before power loss — resuming PAUSE");
                }
                transitionTo(ControllerState::PAUSE);
            } else {
                // Default to IDLE
                transitionTo(ControllerState::IDLE);
            }
        } else {
            // Normal boot - no persisted state to restore
            transitionTo(ControllerState::IDLE);
        }
    }
}

void PlantOSController::handleIdle() {
    // Breathing green - waiting for commands
    // This is the default ready state

    uint32_t elapsed = getStateElapsed();

    // Periodically check PSM for SHUTDOWN or PAUSE states every 5 seconds
    // This handles cases where:
    // 1. State was set while system was running
    // 2. State restoration from INIT failed due to timing issues
    // 3. Component initialization order caused PSM to be unavailable during INIT
    static uint32_t last_psm_check = 0;
    uint32_t now = hal_->getSystemTime();

    if (now - last_psm_check >= 5000) {  // Check every 5 seconds
        last_psm_check = now;

        if (psm_ && psm_->hasEvent()) {
            esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
            std::string eventID(event.eventID);

            ESP_LOGD(TAG, ">>> IDLE PSM CHECK: EventID='%s', Status=%d, Age=%lld sec",
                     eventID.c_str(), event.status, psm_->getEventAge());

            // Check for persistent states that should override IDLE
            if (!eventID.empty()) {
                if (eventID == "STATE_SHUTDOWN") {
                    ESP_LOGW(TAG, ">>> PSM check: SHUTDOWN state detected - transitioning from IDLE");
                    transitionTo(ControllerState::SHUTDOWN);
                    return;
                } else if (eventID == "STATE_PAUSE") {
                    ESP_LOGW(TAG, ">>> PSM check: PAUSE state detected - transitioning from IDLE");
                    transitionTo(ControllerState::PAUSE);
                    return;
                }
            }
        }
    }

    // ========================================================================
    // CALENDAR DAY AUTO-ADVANCE - Sync grow day to real-world local date
    // ========================================================================
    // Runs once the first time NTP time is valid, then every 60s. The calendar
    // manager advances the grow day by the number of local calendar days elapsed
    // since its last-synced date (handles midnight rollover during runtime and
    // catch-up after the device was powered off across one or more midnights).
    if (calendar_manager_ && hal_->hasTime()) {
        uint32_t nowMs = esphome::millis();
        if (!did_initial_date_sync_ || (nowMs - last_date_check_ms_) >= 60000) {
            int32_t today_ordinal = getLocalDateOrdinal();
            if (today_ordinal > 0) {
                calendar_manager_->sync_to_date(today_ordinal);
                did_initial_date_sync_ = true;
                last_date_check_ms_ = nowMs;
            }
        }
    }

    // ========================================================================
    // PERIODIC pH MONITORING - Time-based scheduling aligned to midnight
    // ========================================================================
    // Uses real-time clock to schedule pH checks at fixed intervals from 0:00
    // Example: 2-hour interval → checks at 0:00, 2:00, 4:00, ..., 22:00
    // Supports intervals up to 48 hours (2 days)
    //
    // Falls back to millis()-based scheduling if no time source available
    // ========================================================================

    uint32_t ph_interval_ms = hal_->get_ph_reading_interval();  // Default: 2 hours (7200000 ms)

    // Check if time source is available for clock-based scheduling
    if (hal_->hasTime()) {
        // TIME-BASED SCHEDULING (preferred)
        int64_t current_timestamp = hal_->getCurrentTimestamp();
        uint32_t ph_interval_seconds = ph_interval_ms / 1000;  // Convert ms to seconds

        // Check if we should trigger a pH check
        bool should_trigger = false;

        if (ph_interval_seconds >= 86400) {
            // MULTI-DAY INTERVAL (≥24 hours)
            // Example: 48h interval → check every 2 days at midnight
            // Calculate days since epoch and check if interval has passed
            int64_t days_since_epoch = current_timestamp / 86400;
            int64_t last_check_days = last_ph_check_timestamp_ / 86400;
            int64_t interval_days = ph_interval_seconds / 86400;

            if (days_since_epoch - last_check_days >= interval_days) {
                // Enough days have passed - trigger if we're past midnight
                uint32_t seconds_since_midnight = hal_->getSecondsSinceMidnight();
                if (seconds_since_midnight >= 60) {  // Wait 1 minute after midnight for clock stability
                    should_trigger = true;
                    ESP_LOGI(TAG, "Multi-day pH check: %lld days since last check (interval: %lld days)",
                             days_since_epoch - last_check_days, interval_days);
                }
            }
        } else {
            // DAILY INTERVAL (<24 hours)
            // Example: 2h interval → slots at 0:00, 2:00, 4:00, ..., 22:00
            uint32_t seconds_since_midnight = hal_->getSecondsSinceMidnight();
            uint32_t current_slot = seconds_since_midnight / ph_interval_seconds;

            // Calculate timestamp of current slot boundary
            int64_t day_start = current_timestamp - seconds_since_midnight;
            int64_t current_slot_timestamp = day_start + (current_slot * ph_interval_seconds);

            // Trigger if we've crossed into a new slot since last check
            if (current_slot_timestamp > last_ph_check_timestamp_) {
                should_trigger = true;
                uint32_t slot_hour = (current_slot * ph_interval_seconds) / 3600;
                uint32_t slot_minute = ((current_slot * ph_interval_seconds) % 3600) / 60;
                ESP_LOGI(TAG, "Daily pH check slot: %02u:%02u (slot %u of %u)",
                         slot_hour, slot_minute, current_slot, 86400 / ph_interval_seconds);
            }
        }

        if (should_trigger) {
            last_ph_check_timestamp_ = current_timestamp;
            last_ph_check_time_ = now;  // Update fallback timer too

            if (!auto_ph_correction_enabled_ || (night_mode_enabled_ && isNightModeHours())) {
                ESP_LOGD(TAG, "Automatic pH check skipped — %s",
                         !auto_ph_correction_enabled_ ? "auto pH correction disabled" : "night mode active");
            } else {
                ESP_LOGI(TAG, "========================================================");
                ESP_LOGI(TAG, "  AUTOMATIC pH CHECK (time-based: every %u hours)",
                         ph_interval_ms / 3600000);
                ESP_LOGI(TAG, "========================================================");

                transitionTo(ControllerState::PH_PROCESSING);
                return;  // Exit IDLE immediately
            }
        }
    } else {
        // FALLBACK: millis()-based scheduling (no time source available)
        if (now - last_ph_check_time_ >= ph_interval_ms) {
            last_ph_check_time_ = now;

            if (!auto_ph_correction_enabled_ || (night_mode_enabled_ && isNightModeHours())) {
                ESP_LOGD(TAG, "Automatic pH check skipped — %s",
                         !auto_ph_correction_enabled_ ? "auto pH correction disabled" : "night mode active");
            } else {
                ESP_LOGW(TAG, "========================================================");
                ESP_LOGW(TAG, "  AUTOMATIC pH CHECK (fallback mode: no time source)");
                ESP_LOGW(TAG, "  Using boot-time based interval: every %u hours", ph_interval_ms / 3600000);
                ESP_LOGW(TAG, "========================================================");

                transitionTo(ControllerState::PH_PROCESSING);
                return;  // Exit IDLE immediately
            }
        }
    }

    // ========================================================================
    // POST-FILL EC CHECK - triggered by auto_ec_check_pending_ after WATER_FILLING
    // Requires: auto-feeding enabled, not night mode hours
    // ========================================================================
    if (auto_ec_check_pending_ && auto_feeding_enabled_ && !(night_mode_enabled_ && isNightModeHours())) {
        ESP_LOGI(TAG, "========================================================");
        ESP_LOGI(TAG, "  EC CHECK TRIGGERED (post water fill)");
        ESP_LOGI(TAG, "========================================================");
        transitionTo(ControllerState::EC_PROCESSING);
        return;
    }

    // Check if night mode hours started - transition to NIGHT
    if (night_mode_enabled_ && isNightModeHours()) {
        ESP_LOGI(TAG, "Night mode hours started - transitioning to NIGHT state");
        transitionTo(ControllerState::NIGHT);
        return;
    }

    // ========================================================================
    // SHELLY HEALTH CHECK - Ping Shelly device periodically (every 30 seconds)
    // ========================================================================
    checkShellyHealth();

    // ========================================================================
    // AUTOMATIC FEEDING CHECK - Once per day when water level reaches LOW
    // ========================================================================
    if (shouldTriggerAutoFeeding()) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "AUTO-FEEDING TRIGGERED");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "[AUTO-FEEDING] Water level at LOW - triggering daily feed");

        // Record today's date
        int64_t today = getCurrentDateTimestamp();
        if (today > 0) {
            last_auto_feed_date_ = today;

            // Save to NVS with unique key per date
            if (psm_) {
                char date_key[32];
                snprintf(date_key, sizeof(date_key), "AUTOFEED_%lld", (long long)today);
                psm_->logEvent(date_key, 0);  // 0 = STARTED

                ESP_LOGI(TAG, "[AUTO-FEEDING] Stored trigger date: %lld", (long long)today);
            }
        }

        // Start feeding sequence
        startFeed();
        return;
    }

    // ========================================================================
    // AUTOMATIC WATER FILL CHECK - When EMPTY sensor triggers (Phase 6)
    // ========================================================================
    if (auto_fill_enabled_ && hal_ && hal_->hasWaterLevelSensors()) {
        // Trigger when tank is empty (HIGH=OFF, LOW=OFF, EMPTY=ON)
        // and not in night mode / not auto-feeding about to trigger
        bool high = hal_->readWaterLevelHigh();
        bool low = hal_->readWaterLevelLow();
        bool empty = hal_->readWaterLevelEmpty();
        bool tank_empty = (!high && !low && empty);

        if (tank_empty && !(night_mode_enabled_ && isNightModeHours())) {
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "AUTO-FILL TRIGGERED");
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "[AUTO-FILL] EMPTY sensor active - starting water fill sequence");
            startFillTank();
            return;
        }
    }

    // ========================================================================
    // AUTOMATIC RESERVOIR CHANGE CHECK - Once per week on configured day
    // ========================================================================
    if (shouldTriggerAutoReservoirChange()) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "  AUTO RESERVOIR CHANGE TRIGGERED");
        ESP_LOGI(TAG, "  (Weekly schedule - Day %d)", auto_reservoir_change_day_);
        ESP_LOGI(TAG, "========================================");

        // Record this week's trigger
        int64_t current_week = getCurrentWeekNumber();
        if (current_week > 0) {
            last_auto_reservoir_change_week_ = current_week;

            // Save to NVS with unique key per week
            if (psm_) {
                char week_key[32];
                snprintf(week_key, sizeof(week_key), "AUTORES_%lld", (long long)current_week);
                psm_->logEvent(week_key, 0);  // 0 = STARTED

                ESP_LOGI(TAG, "[AUTO-RES] Stored trigger week: %lld", (long long)current_week);
            }
        }

        // Start reservoir change sequence
        startReservoirChange();
        return;
    }

    // ========================================================================
    // SENSOR HEALTH CHECKS - Periodic (Phase 7)
    // Temperature alerts, plausibility checks, calibration reminders
    // ========================================================================
    if (now - last_sensor_health_check_time_ >= SENSOR_HEALTH_CHECK_INTERVAL_MS) {
        last_sensor_health_check_time_ = now;
        checkTemperatureAlerts();
        checkSensorPlausibility();
        checkWaterLevelPlausibility();
        checkCalibrationReminders();
    }
}

void PlantOSController::handleNight() {
    // Dim breathing green - night mode active
    // Similar to IDLE but no automatic pH monitoring, feeding, or water management

    uint32_t elapsed = getStateElapsed();

    // Periodically check PSM for SHUTDOWN or PAUSE states every 5 seconds
    // This handles cases where:
    // 1. State was set while system was running
    // 2. State restoration from INIT failed due to timing issues
    // 3. Component initialization order caused PSM to be unavailable during INIT
    static uint32_t last_psm_check = 0;
    uint32_t now = hal_->getSystemTime();

    if (now - last_psm_check >= 5000) {  // Check every 5 seconds
        last_psm_check = now;

        if (psm_ && psm_->hasEvent()) {
            esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
            std::string eventID(event.eventID);

            ESP_LOGD(TAG, ">>> NIGHT PSM CHECK: EventID='%s', Status=%d, Age=%lld sec",
                     eventID.c_str(), event.status, psm_->getEventAge());

            // Check for persistent states that should override NIGHT
            if (!eventID.empty()) {
                if (eventID == "STATE_SHUTDOWN") {
                    ESP_LOGW(TAG, ">>> PSM check: SHUTDOWN state detected - transitioning from NIGHT");
                    transitionTo(ControllerState::SHUTDOWN);
                    return;
                } else if (eventID == "STATE_PAUSE") {
                    ESP_LOGW(TAG, ">>> PSM check: PAUSE state detected - transitioning from NIGHT");
                    transitionTo(ControllerState::PAUSE);
                    return;
                }
            }
        }
    }

    // Check if night mode hours ended - transition back to IDLE
    if (!night_mode_enabled_ || !isNightModeHours()) {
        if (!night_mode_enabled_) {
            ESP_LOGI(TAG, "Night mode disabled - transitioning to IDLE state");
        } else {
            ESP_LOGI(TAG, "Night mode hours ended - transitioning to IDLE state");
        }
        transitionTo(ControllerState::IDLE);
        return;
    }

    // During night mode: No automatic pH monitoring, no feeding, no water management
    // Manual operations are still prevented by checking night mode in public API methods
}

void PlantOSController::handleShutdown() {
    // Solid yellow LED handled by LedBehaviorSystem
    // All actuators OFF, calendar/time-based events disabled
    // Persists across power cycles - must use setToIdle() to exit

    uint32_t elapsed = getStateElapsed();

    // On entry: Turn off all pumps and valves for safety (only once!)
    if (!state_entry_executed_) {
        state_entry_executed_ = true;
        turnOffAllPumps();
        ESP_LOGI(TAG, "SHUTDOWN state ACTIVE - all actuators OFF, calendar disabled");

        // Persist SHUTDOWN state to NVS
        if (psm_) {
            ESP_LOGW(TAG, ">>> SAVING TO PSM: STATE_SHUTDOWN (status=1)");
            psm_->logEvent("STATE_SHUTDOWN", 1);  // 1 = ENTERED_SHUTDOWN

            // Verify it was saved
            if (psm_->hasEvent()) {
                auto event = psm_->getLastEvent();
                ESP_LOGW(TAG, ">>> PSM VERIFICATION: EventID='%s', Status=%d",
                         event.eventID, event.status);
            }
        } else {
            ESP_LOGE(TAG, ">>> PSM NOT AVAILABLE - SHUTDOWN STATE NOT PERSISTED!");
        }

        // Update status logger
        status_logger_.updateMaintenanceMode(true);

        return;
    }

    // Stay in this state until setToIdle() is called
    // No automatic exit - persists across reboots
}

void PlantOSController::handlePause() {
    // Solid orange LED handled by LedBehaviorSystem
    // Actuators maintain current state, calendar/time-based events disabled
    // Persists across power cycles - must use setToIdle() to exit

    uint32_t elapsed = getStateElapsed();

    // On entry: Log state change (only once!)
    if (!state_entry_executed_) {
        state_entry_executed_ = true;
        ESP_LOGI(TAG, "PAUSE state ACTIVE - actuators maintained, calendar disabled");

        // Persist PAUSE state to NVS
        if (psm_) {
            ESP_LOGW(TAG, ">>> SAVING TO PSM: STATE_PAUSE (status=1)");
            psm_->logEvent("STATE_PAUSE", 1);  // 1 = ENTERED_PAUSE

            // Verify it was saved
            if (psm_->hasEvent()) {
                auto event = psm_->getLastEvent();
                ESP_LOGW(TAG, ">>> PSM VERIFICATION: EventID='%s', Status=%d",
                         event.eventID, event.status);
            }
        } else {
            ESP_LOGE(TAG, ">>> PSM NOT AVAILABLE - PAUSE STATE NOT PERSISTED!");
        }

        return;
    }

    // Stay in this state until setToIdle() is called
    // No automatic exit - persists across reboots
}

void PlantOSController::handleError() {
    // Fast red flash LED handled by LedBehaviorSystem
    // Error condition with automatic recovery to INIT after 5 seconds

    uint32_t elapsed = getStateElapsed();

    // On entry: Turn off all pumps for safety (only once!)
    if (!state_entry_executed_) {
        state_entry_executed_ = true;
        turnOffAllPumps();
        ESP_LOGE(TAG, "ERROR state: All pumps OFF for safety");
        return;
    }

    // Wait 5 seconds to display error
    if (elapsed >= ERROR_DURATION) {
        if (ENHANCED_ERROR_HANDLING_ENABLED) {
            // Resolve all active alerts (keep history for debugging)
            ESP_LOGI(TAG, "Error timeout - resolving alerts and restarting to INIT");

            // Original alerts
            status_logger_.resolveAlert("PH_CRITICAL");
            status_logger_.resolveAlert("PH_MAX_ATTEMPTS");
            status_logger_.resolveAlert("SENSOR_CRITICAL");
            status_logger_.resolveAlert("PH_SENSOR_CRITICAL");

            // New alerts from Phase 7
            status_logger_.resolveAlert("NO_PH_READINGS");
            status_logger_.resolveAlert("PUMP_REJECTION_ACID");
            status_logger_.resolveAlert("PUMP_REJECTION_NUTRIENT_A");
            status_logger_.resolveAlert("PUMP_REJECTION_NUTRIENT_B");
            status_logger_.resolveAlert("PUMP_REJECTION_NUTRIENT_C");
            status_logger_.resolveAlert("PUMP_REJECTION_WATER_VALVE");
            status_logger_.resolveAlert("PUMP_REJECTION_WASTEWATER");
            status_logger_.resolveAlert("PH_SENSOR_HARDWARE_FAILURE");
            status_logger_.resolveAlert("CALIBRATION_FAILED_MID");
            status_logger_.resolveAlert("CALIBRATION_FAILED_LOW");
            status_logger_.resolveAlert("CALIBRATION_FAILED_HIGH");
            status_logger_.resolveAlert("HARDWARE_HAL_MISSING");

            // Hardware detection alerts from Phase 8
            status_logger_.resolveAlert("HARDWARE_WATER_SENSOR_HIGH");
            status_logger_.resolveAlert("HARDWARE_WATER_SENSOR_EMPTY");
            status_logger_.resolveAlert("HARDWARE_WATER_SENSORS_MISSING");
        } else {
            // Legacy: clear alerts (backward compatible)
            ESP_LOGI(TAG, "Error timeout - clearing alerts and restarting to INIT");
            status_logger_.clearAlert("PH_CRITICAL");
            status_logger_.clearAlert("PH_MAX_ATTEMPTS");
            status_logger_.resolveAlert("SENSOR_CRITICAL");
        }

        transitionTo(ControllerState::INIT);
    }
}

// ============================================================================
// EC Feeding State Handlers
// ============================================================================

// ============================================================================
// pH Processing State Handler
// ============================================================================

// ============================================================================
// Calibration Helper Methods
// ============================================================================

// ============================================================================
// Public API
// ============================================================================

void PlantOSController::startPhCorrection() {
    if (current_state_ != ControllerState::IDLE && current_state_ != ControllerState::NIGHT) {
        ESP_LOGW(TAG, "Cannot start pH correction - system not in IDLE or NIGHT state (state: %d)", static_cast<int>(current_state_));
        return;
    }

    // Prevent pH correction during night mode
    if (current_state_ == ControllerState::NIGHT) {
        ESP_LOGW(TAG, "Cannot start pH correction - system in NIGHT mode");
        return;
    }

    ESP_LOGI(TAG, "Starting pH correction sequence");

    // Reset pH correction state
    ph_attempt_count_ = 0;
    ph_readings_.clear();
    ph_current_ = 0.0f;
    ph_dose_duration_ms_ = 0;
    ph_cycle_start_ph_ = 0.0f;
    ph_cycle_total_ml_ = 0.0f;
    ph_cycle_water_filled_ = false;
    ph_cycle_aborted_ = false;

    // Manual trigger always uses the normal 30s stabilization: clear any stale
    // post-feed/post-fill intent flags that may linger from an aborted auto-flow.
    ph_post_fill_stabilize_ = false;
    ph_post_feed_stabilize_ = false;

    // Initialize operation retry framework (max 3 retries for pH correction)
    initOperationRetry("PH_CORRECTION", 3);

    // Log event for crash recovery persistence
    if (psm_) {
        psm_->logEvent("PH_CORRECTION", 0);  // 0 = STARTED
    }

    transitionTo(ControllerState::PH_MEASURING);
}

void PlantOSController::startPhCalibration() {
    if (current_state_ != ControllerState::IDLE) {
        ESP_LOGW(TAG, "Cannot start pH calibration - system not in IDLE state (state: %s)",
                 getStateAsString().c_str());
        return;
    }

    // Check if sensor hardware is actually connected and responding
    if (!hal_->isPhSensorReady()) {
        ESP_LOGE(TAG, "Cannot start pH calibration - sensor hardware not responding");
        ESP_LOGE(TAG, "Check UART connection (TX=GPIO18, RX=GPIO19) and power to sensor");
        ESP_LOGE(TAG, "Verify sensor shows up in Central Status Logger");
        return;
    }

    ESP_LOGI(TAG, "===============================================");
    ESP_LOGI(TAG, "  STARTING 3-POINT pH CALIBRATION SEQUENCE");
    ESP_LOGI(TAG, "===============================================");
    ESP_LOGI(TAG, "Calibration points: 4.00 (low), 7.00 (mid), 10.01 (high)");
    ESP_LOGI(TAG, "Follow the prompts to insert probe into each solution");

    // Enable verbose mode for detailed UART logging during calibration
    hal_->setPhVerbose(true);
    ESP_LOGI(TAG, "Verbose mode ENABLED for calibration sequence");

    // Reset calibration state
    calib_step_ = CalibrationStep::MID_PROMPT;
    calib_step_start_time_ = esphome::millis();

    // Log event for crash recovery persistence
    if (psm_) {
        psm_->logEvent("PH_CALIBRATION", 0);  // 0 = STARTED
    }

    transitionTo(ControllerState::PH_CALIBRATING);
}

void PlantOSController::startFeeding() {
    if (current_state_ == ControllerState::IDLE) {
        ESP_LOGI(TAG, "Starting feeding sequence");

        // Log event for crash recovery persistence
        if (psm_) {
            psm_->logEvent("FEEDING", 0);  // 0 = STARTED
        }

        transitionTo(ControllerState::FEEDING);
    } else if (current_state_ == ControllerState::NIGHT) {
        ESP_LOGW(TAG, "Cannot start feeding - system in NIGHT mode");
    } else {
        ESP_LOGW(TAG, "Cannot start feeding - system busy");
    }
}

void PlantOSController::startMotherNutrients(float liters) {
    if (current_state_ != ControllerState::IDLE) {
        ESP_LOGW(TAG, "[MOTHER_MIX] Cannot start - system busy (state: %s)", getStateAsString().c_str());
        return;
    }
    if (liters <= 0.0f || liters > 15.0f) {
        ESP_LOGW(TAG, "[MOTHER_MIX] Invalid volume: %.1f L (must be 1-15)", liters);
        return;
    }

    ec_dose_A_ml_ = MOTHER_MIX_A_ML_PER_L * liters;
    ec_dose_B_ml_ = MOTHER_MIX_B_ML_PER_L * liters;
    ec_dose_C_ml_ = MOTHER_MIX_C_ML_PER_L * liters;
    ec_total_ml_per_L_ = 0.0f;
    ec_attempt_count_ = 0;
    feeding_manual_mode_ = false;
    is_mother_mix_ = true;

    ESP_LOGI(TAG, "===============================================");
    ESP_LOGI(TAG, "  MOTHER PLANT NUTRIENT MIX");
    ESP_LOGI(TAG, "===============================================");
    ESP_LOGI(TAG, "  Volume : %.1f L", liters);
    ESP_LOGI(TAG, "  Dose A : %.2f mL  (%.2f mL/L)", ec_dose_A_ml_, MOTHER_MIX_A_ML_PER_L);
    ESP_LOGI(TAG, "  Dose B : %.2f mL  (%.2f mL/L)", ec_dose_B_ml_, MOTHER_MIX_B_ML_PER_L);
    ESP_LOGI(TAG, "  Dose C : %.2f mL  (%.2f mL/L)", ec_dose_C_ml_, MOTHER_MIX_C_ML_PER_L);
    ESP_LOGI(TAG, "  Sequence: A → B → C → IDLE (no EC loop, no mixing wait)");
    ESP_LOGI(TAG, "===============================================");

    if (psm_) psm_->logEvent("MOTHER_MIX", 0);
    transitionTo(ControllerState::EC_FEEDING);
}

void PlantOSController::startFillTank() {
    if (current_state_ == ControllerState::IDLE) {
        // SAFETY: Never fill without water level sensors - the valve would run
        // blind on a timeout with no feedback, risking tank overflow
        if (!hal_ || !hal_->hasWaterLevelSensors()) {
            ESP_LOGE(TAG, "Cannot start fill - water level sensors unavailable (overflow risk)");
            return;
        }

        ESP_LOGI(TAG, "Starting tank fill with auto pH correction");

        // Set flag for auto pH correction after fill
        auto_ph_correction_pending_ = true;

        // Log event for crash recovery persistence
        if (psm_) {
            psm_->logEvent("WATER_FILL", 0);  // 0 = STARTED
        }

        transitionTo(ControllerState::WATER_FILLING);
    } else if (current_state_ == ControllerState::NIGHT) {
        ESP_LOGW(TAG, "Cannot start fill - system in NIGHT mode");
    } else {
        ESP_LOGW(TAG, "Cannot start fill - system busy");
    }
}

void PlantOSController::startEmptyTank() {
    if (current_state_ != ControllerState::IDLE && current_state_ != ControllerState::NIGHT) {
        ESP_LOGW(TAG, "Cannot start tank drain - system busy");
        return;
    }

    if (current_state_ == ControllerState::NIGHT) {
        ESP_LOGW(TAG, "Cannot start tank drain - system in NIGHT mode");
        return;
    }

    // The wastewater pump is a Shelly socket - no Shelly, no drain
    if (!hal_->isShellyEnabled()) {
        ESP_LOGE(TAG, "Cannot start tank drain - Shelly integration disabled (wastewater pump unavailable)");
        return;
    }

    ESP_LOGI(TAG, "========================================================");
    ESP_LOGI(TAG, "  STARTING TANK DRAIN");
    ESP_LOGI(TAG, "========================================================");
    ESP_LOGI(TAG, "Draining tank via wastewater pump until EMPTY sensor triggers");
    ESP_LOGI(TAG, "");

    // Log event for crash recovery persistence
    if (psm_) {
        psm_->logEvent("WATER_EMPTY", 0);  // 0 = STARTED
    }

    transitionTo(ControllerState::WATER_EMPTYING);
}

void PlantOSController::startFeed() {
    if (current_state_ != ControllerState::IDLE && current_state_ != ControllerState::NIGHT) {
        ESP_LOGW(TAG, "Cannot start feed - system busy");
        return;
    }

    // Prevent feed during night mode
    if (current_state_ == ControllerState::NIGHT) {
        ESP_LOGW(TAG, "Cannot start feed - system in NIGHT mode");
        return;
    }

    if (current_state_ == ControllerState::IDLE) {
        ESP_LOGI(TAG, "===============================================");
        ESP_LOGI(TAG, "  STARTING FEED OPERATION");
        ESP_LOGI(TAG, "===============================================");
        ESP_LOGI(TAG, "Sequence: Fill tank → Add nutrients → pH correction");

        // SAFETY CHECK: Verify tank is empty (both sensors OFF) before filling
        if (hal_ && hal_->hasWaterLevelSensors()) {
            bool high_sensor = hal_->readWaterLevelHigh();
            bool low_sensor = hal_->readWaterLevelLow();

            if (high_sensor || low_sensor) {
                ESP_LOGE(TAG, "FEED ABORTED: Tank must be empty before starting!");
                ESP_LOGE(TAG, "Water level sensors: HIGH=%s, LOW=%s",
                         high_sensor ? "ON" : "OFF", low_sensor ? "ON" : "OFF");
                ESP_LOGE(TAG, "Please drain the tank manually until both sensors show OFF");
                return;
            }

            ESP_LOGI(TAG, "Water level check PASSED: Tank is empty (both sensors OFF)");
        } else {
            ESP_LOGW(TAG, "Water level sensors not available - skipping pre-fill check");
        }

        // Set flag for auto pH correction after feeding
        auto_ph_correction_pending_ = true;

        // Set operation context: normal feed uses delta volume (LOW→HIGH)
        is_reservoir_change_ = false;
        ESP_LOGI(TAG, "Operation context: NORMAL FEED (using tank volume delta)");

        // Log event for crash recovery persistence
        if (psm_) {
            psm_->logEvent("FEED_OPERATION", 0);  // 0 = STARTED
        }

        // Start with filling
        transitionTo(ControllerState::FEED_FILLING);
    } else {
        ESP_LOGW(TAG, "Cannot start Feed operation - system busy");
    }
}

void PlantOSController::startEcCheck() {
    if (current_state_ != ControllerState::IDLE) {
        ESP_LOGW(TAG, "Cannot start EC check - system not in IDLE state (state: %s)",
                 getStateAsString().c_str());
        return;
    }

    ESP_LOGI(TAG, "Manual EC check triggered");
    ec_pre_feeding_ = 0.0f;
    ec_total_ml_per_L_ = 0.0f;
    ec_attempt_count_ = 0;
    ec_cycle_water_filled_ = false;
    transitionTo(ControllerState::EC_PROCESSING);
}

void PlantOSController::startEcCalibration() {
    if (current_state_ != ControllerState::IDLE) {
        ESP_LOGW(TAG, "Cannot start EC calibration - system not in IDLE state (state: %s)",
                 getStateAsString().c_str());
        return;
    }

    if (!hal_->hasECValue()) {
        ESP_LOGE(TAG, "Cannot start EC calibration - no EC sensor reading available");
        status_logger_.updateAlertStatus("EC_CALIB_ERROR", "EC sensor not available for calibration");
        return;
    }

    ESP_LOGI(TAG, "Starting EC single-point calibration (target: %.1f uS/cm)", ec_calib_target_);
    ec_calib_step_ = ECCalibStep::RINSE_PROMPT;
    ec_calib_step_start_time_ = hal_->getSystemTime();
    ec_calib_readings_.clear();
    ec_calib_last_reading_time_ = 0;
    ec_calib_prompt_shown_ = false;
    transitionTo(ControllerState::EC_CALIBRATING);
}

void PlantOSController::startReservoirChange() {
    if (current_state_ != ControllerState::IDLE && current_state_ != ControllerState::NIGHT) {
        ESP_LOGW(TAG, "Cannot start Reservoir Change - system busy");
        return;
    }

    // Prevent reservoir change during night mode
    if (current_state_ == ControllerState::NIGHT) {
        ESP_LOGW(TAG, "Cannot start Reservoir Change - system in NIGHT mode");
        return;
    }

    // The wastewater pump is a Shelly socket - no Shelly, no drain
    if (!hal_->isShellyEnabled()) {
        ESP_LOGE(TAG, "Cannot start Reservoir Change - Shelly integration disabled (wastewater pump unavailable)");
        return;
    }

    ESP_LOGI(TAG, "========================================================");
    ESP_LOGI(TAG, "  STARTING RESERVOIR CHANGE SEQUENCE");
    ESP_LOGI(TAG, "========================================================");
    ESP_LOGI(TAG, "Sequence: Empty tank → Fill → Add nutrients → pH correction");
    ESP_LOGI(TAG, "");

    // Set flag for auto pH correction after feeding
    auto_ph_correction_pending_ = true;

    // Set operation context: reservoir change uses total volume (EMPTY→HIGH)
    is_reservoir_change_ = true;
    ESP_LOGI(TAG, "Operation context: RESERVOIR CHANGE (using total tank volume)");

    // Log event for crash recovery persistence
    if (psm_) {
        psm_->logEvent("RESERVOIR_CHANGE", 0);  // 0 = STARTED
    }

    // Start with emptying tank using wastewater pump (Shelly Socket 2)
    ESP_LOGI(TAG, "Starting empty phase - activating wastewater pump via Shelly");
    transitionTo(ControllerState::WATER_EMPTYING);
}

void PlantOSController::emergencyStop() {
    ESP_LOGW(TAG, "========================================================");
    ESP_LOGW(TAG, "  EMERGENCY STOP - ALL PUMPS OFF");
    ESP_LOGW(TAG, "========================================================");
    turnOffAllPumps();
    transitionTo(ControllerState::SHUTDOWN);
}

void PlantOSController::setToShutdown() {
    ESP_LOGI(TAG, "Transitioning to SHUTDOWN state");
    transitionTo(ControllerState::SHUTDOWN);
}

void PlantOSController::setToPause() {
    ESP_LOGI(TAG, "Transitioning to PAUSE state");
    transitionTo(ControllerState::PAUSE);
}

void PlantOSController::setToIdle() {
    if (current_state_ == ControllerState::SHUTDOWN || current_state_ == ControllerState::PAUSE) {
        ESP_LOGI(TAG, "Exiting %s state to IDLE",
                 current_state_ == ControllerState::SHUTDOWN ? "SHUTDOWN" : "PAUSE");

        // Clear PSM persistence
        if (psm_) {
            psm_->clearEvent();
        }

        // Update status logger
        status_logger_.updateMaintenanceMode(false);

        transitionTo(ControllerState::IDLE);
    } else {
        ESP_LOGW(TAG, "setToIdle() called but not in SHUTDOWN or PAUSE state (current: %d)",
                 static_cast<int>(current_state_));
    }
}

void PlantOSController::activateAirPump() {
    ESP_LOGI(TAG, "Activating air pump (WiFi on_connect)");

    // Send single command to turn on air pump
    // Uses unlimited duration (0) as this is the initial activation
    if (!requestPump(AIR_PUMP, true, 0, true)) {  // forceExecute=true to bypass debouncing
        ESP_LOGW(TAG, "Air pump activation failed - pump may not be configured");
    }
}

void PlantOSController::resetToInit() {
    ESP_LOGI(TAG, "Manual reset requested");
    turnOffAllPumps();
    transitionTo(ControllerState::INIT);
}

void PlantOSController::configureStatusLogger(bool enableReports, uint32_t reportIntervalMs, bool verboseMode, bool enable420Mode) {
    status_logger_.configure(enableReports, reportIntervalMs, verboseMode);
    status_logger_.set420Mode(enable420Mode);
}

std::string PlantOSController::getStateAsString() const {
    // State name mapping for string representation
    const char* state_names[] = {
        "INIT", "IDLE", "NIGHT", "SHUTDOWN", "PAUSE", "ERROR",
        "PH_PROCESSING", "PH_MEASURING", "PH_CALCULATING", "PH_INJECTING", "PH_MIXING", "PH_CALIBRATING",
        "FEEDING", "WATER_FILLING", "WATER_EMPTYING", "FEED_FILLING",
        "EC_PROCESSING", "EC_CALCULATING", "EC_FEEDING", "EC_MIXING", "EC_MEASURING",
        "EC_CALIBRATING"
    };

    int state_index = static_cast<int>(current_state_);
    if (state_index >= 0 && state_index < 22) {
        return std::string(state_names[state_index]);
    }

    return "UNKNOWN";
}

// ============================================================================
// State Transition
// ============================================================================

void PlantOSController::transitionTo(ControllerState newState) {
    if (newState == current_state_) {
        return; // Already in this state
    }

    // State name mapping for readable logging
    const char* state_names[] = {
        "INIT", "IDLE", "NIGHT", "SHUTDOWN", "PAUSE", "ERROR",
        "PH_PROCESSING", "PH_MEASURING", "PH_CALCULATING", "PH_INJECTING", "PH_MIXING", "PH_CALIBRATING",
        "FEEDING", "WATER_FILLING", "WATER_EMPTYING", "FEED_FILLING",
        "EC_PROCESSING", "EC_CALCULATING", "EC_FEEDING", "EC_MIXING", "EC_MEASURING",
        "EC_CALIBRATING"
    };

    const char* oldStateName = state_names[static_cast<int>(current_state_)];
    const char* newStateName = state_names[static_cast<int>(newState)];

    ESP_LOGI(TAG, "State transition: %s → %s", oldStateName, newStateName);

    // Update status logger with new state
    status_logger_.updateControllerState(newStateName);

    // Verbose mode: Log detailed state transition info
    if (status_logger_.isVerboseMode()) {
        ESP_LOGI(TAG, "[VERBOSE] State change details:");
        ESP_LOGI(TAG, "[VERBOSE]   Previous state: %s", oldStateName);
        ESP_LOGI(TAG, "[VERBOSE]   New state: %s", newStateName);
        ESP_LOGI(TAG, "[VERBOSE]   Time in previous state: %u ms", getStateElapsed());
    }

    current_state_ = newState;
    state_start_time_ = esphome::millis();
    state_counter_ = 0;
    state_entry_executed_ = false;  // Reset entry flag for new state

    // LED behavior transition is handled automatically in loop()
    // via led_behaviors_->update()
}

uint32_t PlantOSController::getStateElapsed() const {
    return esphome::millis() - state_start_time_;
}

// ============================================================================
// Actuator Control Helpers
// ============================================================================

bool PlantOSController::requestPump(const std::string& pumpId, bool state, uint32_t durationSec, bool forceExecute) {
    if (!safety_gate_) {
        ESP_LOGW(TAG, "SafetyGate not available - cannot control %s", pumpId.c_str());
        return false;
    }

    // Verbose mode: Log actuator commands
    if (status_logger_.isVerboseMode()) {
        ESP_LOGI(TAG, "[VERBOSE] Actuator command: %s → %s for %u seconds (force=%s)",
                 pumpId.c_str(), state ? "ON" : "OFF", durationSec, forceExecute ? "YES" : "NO");
    }

    return safety_gate_->executeCommand(pumpId.c_str(), state, durationSec, forceExecute);
}

bool PlantOSController::requestPumpAdaptive(const std::string& pumpId, bool state, uint32_t requestedDurationSec, bool forceExecute) {
    // If enhanced error handling is disabled, use legacy behavior
    if (!ENHANCED_ERROR_HANDLING_ENABLED) {
        return requestPump(pumpId, state, requestedDurationSec, forceExecute);
    }

    // Try original request first
    bool approved = safety_gate_->executeCommand(pumpId.c_str(), state, requestedDurationSec, forceExecute);
    if (approved) {
        // Success - verbose log if enabled
        if (status_logger_.isVerboseMode()) {
            ESP_LOGI(TAG, "[VERBOSE] Actuator command approved: %s → %s for %u seconds",
                     pumpId.c_str(), state ? "ON" : "OFF", requestedDurationSec);
        }
        return true;
    }

    // Request was rejected - analyze rejection type
    bool current_state = safety_gate_->getState(pumpId.c_str());

    // Check for debouncing rejection (actuator already in requested state)
    if (current_state == state) {
        // Debouncing rejection - NO RETRY (already executing)
        ESP_LOGD(TAG, "%s debouncing rejection - already %s (no retry)",
                 pumpId.c_str(), state ? "ON" : "OFF");
        return false;
    }

    // Duration violation - TRY TO ADAPT
    if (state && requestedDurationSec > 0) {
        uint32_t adapted_duration = safety_gate_->getAdaptedDuration(pumpId.c_str(), requestedDurationSec);

        // Check if adaptation is possible
        if (adapted_duration > 0 && adapted_duration < requestedDurationSec) {
            // Duration can be adapted - log comprehensive error with adaptation
            status_logger_.updateAlertWithContext(
                "DURATION_ADAPTED_" + pumpId,
                "Duration adapted to " + std::to_string(adapted_duration) + "s",
                "SafetyGate rejected: Duration " + std::to_string(requestedDurationSec) +
                    "s exceeds max " + std::to_string(adapted_duration) + "s",
                "System adapted duration automatically. Consider increasing max duration in config or reducing dose.",
                retry_state_.getContextString(),
                "Retrying with adapted duration " + std::to_string(adapted_duration) + "s",
                0
            );

            // Retry with adapted duration
            approved = safety_gate_->executeCommand(pumpId.c_str(), state, adapted_duration, forceExecute);

            if (approved) {
                // Adaptation successful - mark alert as resolved
                ESP_LOGI(TAG, "%s duration adapted successfully: %us → %us",
                         pumpId.c_str(), requestedDurationSec, adapted_duration);
                status_logger_.resolveAlert("DURATION_ADAPTED_" + pumpId);
                return true;
            } else {
                // Even adapted duration was rejected (should not happen)
                ESP_LOGE(TAG, "%s rejected even after adaptation to %us",
                         pumpId.c_str(), adapted_duration);
            }
        }
    }

    // Could not adapt or adaptation failed - comprehensive error
    std::string rejection_reason = "SafetyGate rejected " + pumpId + " command";
    if (state && requestedDurationSec > 0) {
        uint32_t max_duration = safety_gate_->getMaxDurationSeconds(pumpId.c_str());
        if (max_duration > 0 && requestedDurationSec > max_duration) {
            rejection_reason += " (duration " + std::to_string(requestedDurationSec) +
                              "s exceeds max " + std::to_string(max_duration) + "s)";
        } else {
            rejection_reason += " (unknown reason - check SafetyGate logs)";
        }
    } else {
        rejection_reason += " (turn-off command or zero duration)";
    }

    status_logger_.updateAlertWithContext(
        "SAFETY_GATE_REJECT_" + pumpId,
        rejection_reason,
        "SafetyGate rejected command: " + pumpId + " " + (state ? "ON" : "OFF") +
            " for " + std::to_string(requestedDurationSec) + "s",
        "Check SafetyGate configuration, verify pump is not stuck, check duration limits in plantOS.yaml",
        retry_state_.getContextString(),
        "Operation aborted. Manual intervention may be required.",
        0
    );

    return false;
}

bool PlantOSController::requestValve(const std::string& valveId, bool state, uint32_t durationSec) {
    if (!safety_gate_) {
        ESP_LOGW(TAG, "SafetyGate not available - cannot control %s", valveId.c_str());
        return false;
    }

    // Verbose mode: Log actuator commands
    if (status_logger_.isVerboseMode()) {
        ESP_LOGI(TAG, "[VERBOSE] Valve command: %s → %s for %u seconds",
                 valveId.c_str(), state ? "OPEN" : "CLOSED", durationSec);
    }

    return safety_gate_->executeCommand(valveId.c_str(), state, durationSec);
}

void PlantOSController::turnOffAllPumps() {
    ESP_LOGI(TAG, "Emergency stop - turning off all pumps");

    if (!safety_gate_) {
        return;
    }

    // Turn off all pumps
    requestPump(ACID_PUMP, false);
    requestPump(NUTRIENT_PUMP_A, false);
    requestPump(NUTRIENT_PUMP_B, false);
    requestPump(NUTRIENT_PUMP_C, false);
    requestPump(WASTEWATER_PUMP, false);

    // Stop any running AirPump Shelly sequence and turn OFF
    if (safety_gate_) {
        if (safety_gate_->stopAirPumpSequence(false)) {
            ESP_LOGI(TAG, "AirPump Shelly sequence stopped - pump OFF");
        } else {
            // Debounced (already OFF) or deferred - use simple command as fallback
            requestPump(AIR_PUMP, false);
        }
    } else {
        // Fallback to simple command if SafetyGate not available
        requestPump(AIR_PUMP, false);
    }

    // Close all valves
    requestValve(WATER_VALVE, false);
}

// ============================================================================
// Sensor Helpers
// ============================================================================

float PlantOSController::readPH() {
    if (!hal_) {
        return 0.0f;
    }

    float ph = hal_->readPH();

    // Verbose mode: Log sensor readings (sample every 10th reading to avoid spam)
    static uint32_t reading_counter = 0;
    if (status_logger_.isVerboseMode() && (++reading_counter % 10 == 0)) {
        ESP_LOGI(TAG, "[VERBOSE] pH sensor reading: %.2f", ph);
    }

    return ph;
}

bool PlantOSController::hasPhValue() {
    if (!hal_) {
        return false;
    }
    return hal_->hasPhValue();
}

// ============================================================================
// Grow Cycle Helpers
// ============================================================================

int32_t PlantOSController::getLocalDateOrdinal() {
    if (time_source_ == nullptr) {
        return 0;
    }

    auto now = time_source_->now();
    if (!now.is_valid()) {
        return 0;
    }

    // now.year / now.month / now.day_of_month are in the configured local
    // timezone (SNTP applies the TZ). Convert the local civil date to a
    // days-since-epoch ordinal using Howard Hinnant's days_from_civil algorithm.
    // This subtracts cleanly across month/year boundaries and is DST-robust.
    int32_t y = static_cast<int32_t>(now.year);
    uint32_t m = now.month;
    uint32_t d = now.day_of_month;

    y -= (m <= 2);
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = static_cast<uint32_t>(y - era * 400);                  // [0, 399]
    uint32_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;       // [0, 365]
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                 // [0, 146096]
    int32_t days = era * 146097 + static_cast<int32_t>(doe) - 719468;     // days since 1970-01-01

    return days;
}

uint8_t PlantOSController::getCurrentGrowDay() {
    // Automatic day calculation if grow start date and time source are configured
    if (grow_start_timestamp_ > 0 && time_source_ != nullptr) {
        // Get current time from NTP
        auto now = time_source_->now();

        // Check if time is valid (NTP synchronized)
        if (now.is_valid()) {
            // Get current Unix timestamp
            int64_t current_timestamp = now.timestamp;

            // Calculate days elapsed since grow start
            int64_t seconds_elapsed = current_timestamp - grow_start_timestamp_;
            int32_t days_elapsed = static_cast<int32_t>(seconds_elapsed / 86400);  // 86400 seconds per day

            // Handle negative elapsed time (clock not set or start date in future)
            if (days_elapsed < 0) {
                ESP_LOGW(TAG, "Grow start date is in the future or clock not synchronized - using day 1");
                return 1;
            }

            // Calculate current day (1-120, wraps around)
            uint8_t current_day = static_cast<uint8_t>((days_elapsed % 120) + 1);

            ESP_LOGD(TAG, "Auto-calculated grow day: %d (elapsed: %d days since start)",
                     current_day, days_elapsed);

            return current_day;
        } else {
            ESP_LOGW(TAG, "NTP time not synchronized yet - falling back to calendar day counter");
        }
    }

    // Fallback: Use calendar manager's manual day counter (NVS-persisted)
    if (calendar_manager_) {
        uint8_t manual_day = calendar_manager_->get_current_day();
        ESP_LOGD(TAG, "Using calendar manager day counter: %d", manual_day);
        return manual_day;
    }

    // Ultimate fallback: Day 1
    ESP_LOGW(TAG, "No grow start date or calendar manager - defaulting to day 1");
    return 1;
}

// ============================================================================
// pH Correction Helpers
// ============================================================================

bool PlantOSController::isNightModeHours() const {
    // Check if we're currently within night mode hours.
    // Priority: CalendarManager light schedule (if available) → YAML config fallback.

    if (!time_source_) {
        ESP_LOGW(TAG, "Time source not configured - cannot determine night mode hours");
        return false;
    }

    auto now = time_source_->now();
    if (!now.is_valid()) {
        ESP_LOGW(TAG, "NTP time not synchronized yet - cannot determine night mode hours");
        return false;
    }

    // Phase 6: Use CalendarManager light schedule if available (minute resolution)
    if (calendar_manager_) {
        auto schedule = calendar_manager_->get_today_schedule();
        // light_on_time / light_off_time are minutes from midnight (e.g., 360 = 06:00)
        // Night = light is OFF = outside light_on…light_off window
        if (schedule.light_on_time != schedule.light_off_time) {
            int current_minutes = now.hour * 60 + now.minute;
            bool light_is_on;
            if (schedule.light_on_time < schedule.light_off_time) {
                // Normal day schedule (e.g., 06:00–22:00)
                light_is_on = (current_minutes >= schedule.light_on_time &&
                               current_minutes < schedule.light_off_time);
            } else {
                // Light-on wraps midnight (e.g., 22:00–06:00)
                light_is_on = (current_minutes >= schedule.light_on_time ||
                               current_minutes < schedule.light_off_time);
            }
            return !light_is_on;  // Night = light off
        }
    }

    // Fallback: YAML-configured hour-based night window
    uint8_t current_hour = now.hour;
    if (night_mode_start_hour_ > night_mode_end_hour_) {
        // Spans midnight (e.g., 22:00–08:00)
        return (current_hour >= night_mode_start_hour_) || (current_hour < night_mode_end_hour_);
    } else {
        // Same-day range (e.g., 01:00–06:00)
        return (current_hour >= night_mode_start_hour_) && (current_hour < night_mode_end_hour_);
    }
}

// ============================================================================
// Hardware Detection and Monitoring
// ============================================================================

// ============================================================================
// Shelly Health Check
// ============================================================================

// ============================================================================
// Sensor Health Checks (Phase 7)
// ============================================================================

// ============================================================================
// Operation Retry Framework Helpers
// ============================================================================

// ============================================================================
// Automatic Feeding System
// ============================================================================

bool PlantOSController::shouldTriggerAutoFeeding() {
    // ========================================================================
    // CHECK 1: Auto-feeding must be enabled
    // ========================================================================
    if (!auto_feeding_enabled_) {
        return false;
    }

    // ========================================================================
    // CHECK 2: Must be in IDLE state (not busy)
    // ========================================================================
    if (current_state_ != ControllerState::IDLE) {
        return false;
    }

    // ========================================================================
    // CHECK 3: Not in NIGHT mode hours
    // ========================================================================
    if (night_mode_enabled_ && isNightModeHours()) {
        return false;
    }

    // ========================================================================
    // CHECK 4: Water level sensors must be available
    // ========================================================================
    if (!hal_ || !hal_->hasWaterLevelSensors()) {
        return false;
    }

    // ========================================================================
    // CHECK 5: Water level must be LOW (HIGH=OFF, LOW=OFF, EMPTY=ON)
    // ========================================================================
    bool high = hal_->readWaterLevelHigh();
    bool low = hal_->readWaterLevelLow();
    bool empty = hal_->readWaterLevelEmpty();

    bool is_low_level = (!high && !low && empty);
    if (!is_low_level) {
        return false;
    }

    // ========================================================================
    // CHECK 6: Not already fed today
    // ========================================================================
    int64_t today = getCurrentDateTimestamp();
    if (today == 0) {
        ESP_LOGW(TAG, "[AUTO-FEEDING] NTP time unavailable - cannot check date");
        return false;
    }

    // Check in-memory cache first
    if (last_auto_feed_date_ == today) {
        return false;
    }

    // ========================================================================
    // CHECK 7: Verify no NVS event for today (power cycle recovery)
    // ========================================================================
    if (psm_) {
        char date_key[32];
        snprintf(date_key, sizeof(date_key), "AUTOFEED_%lld", (long long)today);

        esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
        if (strncmp(event.eventID, date_key, sizeof(event.eventID)) == 0) {
            // Event exists - feeding already triggered today
            ESP_LOGI(TAG, "[AUTO-FEEDING] Already fed today - cached from NVS");
            last_auto_feed_date_ = today;  // Update cache
            return false;
        }
    }

    // All checks passed - trigger feeding
    return true;
}

int64_t PlantOSController::getCurrentDateTimestamp() {
    // Use the LOCAL calendar date (timezone-adjusted) so the daily feeding limit
    // rolls over at local midnight, consistent with the calendar day advance.
    int32_t today_ordinal = getLocalDateOrdinal();

    if (today_ordinal <= 0) {
        ESP_LOGW(TAG, "[AUTO-FEEDING] NTP time unavailable");
        return 0;
    }

    // Return a unique, stable per-local-day key (local days since epoch scaled to
    // a seconds-granularity timestamp). Used only for equality + NVS key purposes.
    return static_cast<int64_t>(today_ordinal) * 86400;
}

void PlantOSController::setAutoPhCorrectionEnabled(bool enabled) {
    auto_ph_correction_enabled_ = enabled;

    if (psm_) {
        psm_->saveState(NVS_KEY_AUTO_PH_CORR, enabled);
    }

    ESP_LOGI(TAG, "Auto pH correction %s", enabled ? "ENABLED" : "DISABLED");
}

void PlantOSController::setAutoFillEnabled(bool enabled) {
    auto_fill_enabled_ = enabled;

    if (psm_) {
        psm_->saveState(NVS_KEY_AUTO_FILL_ENABLE, enabled);
    }

    ESP_LOGI(TAG, "Auto-fill %s", enabled ? "ENABLED" : "DISABLED");
}

void PlantOSController::setAutoFeedingEnabled(bool enabled) {
    auto_feeding_enabled_ = enabled;

    if (psm_) {
        psm_->saveState(NVS_KEY_AUTO_FEED_ENABLE, enabled);
    }

    ESP_LOGI(TAG, "Auto-feeding %s", enabled ? "ENABLED" : "DISABLED");
}

// ============================================================================
// Auto Reservoir Change (Weekly)
// ============================================================================

void PlantOSController::setAutoReservoirChangeEnabled(bool enabled) {
    auto_reservoir_change_enabled_ = enabled;

    if (psm_) {
        psm_->saveState(NVS_KEY_AUTO_RES_ENABLE, enabled);
    }

    ESP_LOGI(TAG, "Auto reservoir change %s", enabled ? "ENABLED" : "DISABLED");
}

void PlantOSController::setAutoReservoirChangeDay(uint8_t day) {
    if (day > 6) {
        ESP_LOGW(TAG, "Invalid day of week %d, clamping to 6 (Saturday)", day);
        day = 6;
    }

    auto_reservoir_change_day_ = day;

    if (psm_) {
        psm_->saveState(NVS_KEY_AUTO_RES_DAY, static_cast<int>(day));
    }

    const char* day_names[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    ESP_LOGI(TAG, "Auto reservoir change day set to: %d (%s)", day, day_names[day]);
}

bool PlantOSController::shouldTriggerAutoReservoirChange() {
    // ========================================================================
    // CHECK 1: Auto reservoir change must be enabled
    // ========================================================================
    if (!auto_reservoir_change_enabled_) {
        return false;
    }

    // ========================================================================
    // CHECK 2: Must be in IDLE state (not busy)
    // ========================================================================
    if (current_state_ != ControllerState::IDLE) {
        return false;
    }

    // ========================================================================
    // CHECK 3: Not in NIGHT mode hours
    // ========================================================================
    if (night_mode_enabled_ && isNightModeHours()) {
        return false;
    }

    // ========================================================================
    // CHECK 4: Today must be the configured day of week
    // ========================================================================
    uint8_t today_dow = getCurrentDayOfWeek();
    if (today_dow == 255) {
        return false;  // Time unavailable
    }

    if (today_dow != auto_reservoir_change_day_) {
        return false;  // Not the right day
    }

    // ========================================================================
    // CHECK 5: Not already done this week
    // ========================================================================
    int64_t current_week = getCurrentWeekNumber();
    if (current_week == 0) {
        ESP_LOGW(TAG, "[AUTO-RES] NTP time unavailable - cannot check week");
        return false;
    }

    // Check in-memory cache first
    if (last_auto_reservoir_change_week_ == current_week) {
        return false;  // Already done this week
    }

    // ========================================================================
    // CHECK 6: Verify no NVS event for this week (power cycle recovery)
    // ========================================================================
    if (psm_) {
        char week_key[32];
        snprintf(week_key, sizeof(week_key), "AUTORES_%lld", (long long)current_week);

        esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
        if (strncmp(event.eventID, week_key, sizeof(event.eventID)) == 0) {
            // Event exists - reservoir change already triggered this week
            ESP_LOGI(TAG, "[AUTO-RES] Already done this week - cached from NVS");
            last_auto_reservoir_change_week_ = current_week;  // Update cache
            return false;
        }
    }

    // All checks passed - trigger reservoir change
    ESP_LOGI(TAG, "[AUTO-RES] All conditions met - triggering reservoir change");
    return true;
}

int64_t PlantOSController::getCurrentWeekNumber() {
    if (!hal_) {
        return 0;
    }

    // Get current Unix timestamp
    int64_t now = hal_->getCurrentTimestamp();

    if (now == 0) {
        ESP_LOGW(TAG, "[AUTO-RES] NTP time unavailable");
        return 0;
    }

    // Calculate week number (weeks since Unix epoch)
    // Unix epoch started on Thursday, so we adjust by 4 days to align week boundaries
    int64_t days_since_epoch = now / 86400;
    int64_t weeks_since_epoch = days_since_epoch / 7;

    return weeks_since_epoch;
}

uint8_t PlantOSController::getCurrentDayOfWeek() {
    if (!hal_) {
        return 255;
    }

    // Get current Unix timestamp
    int64_t now = hal_->getCurrentTimestamp();

    if (now == 0) {
        return 255;
    }

    // Unix epoch (Jan 1, 1970) was a Thursday (day 4)
    // (days_since_epoch + 4) % 7 gives: 0=Sunday, 1=Monday, ..., 6=Saturday
    int64_t days_since_epoch = now / 86400;
    return (days_since_epoch + 4) % 7;
}

} // namespace plantos_controller
