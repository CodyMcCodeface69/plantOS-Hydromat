#include "controller.h"
#include "esphome/components/plantos_hal/hal.h"
#include "esphome/components/actuator_safety_gate/ActuatorSafetyGate.h"
#include "esphome/components/persistent_state_manager/persistent_state_manager.h"
#include "esphome/components/ezo_ph_uart/ezo_ph_uart.h"
#include "esphome/components/calendar_manager/CalendarManager.h"
#include "esphome/components/time/real_time_clock.h"
#include <algorithm> // for std::sort, std::min, std::max

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

        // Initialize last feed date (will be loaded from NVS on first check)
        last_auto_feed_date_ = 0;
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

    // Check if we should print periodic status report
    if (status_logger_.shouldPrintStatusReport()) {
        // Update status logger with current pH before printing
        if (hal_->hasPhValue()) {
            status_logger_.updateStatus(hal_->readPH(), "");
        }

        // Update UART hardware status (EZO pH sensor)
        if (ph_sensor_) {
            std::vector<UARTDeviceInfo> uartDevices;
            std::string status = "";

            // Check if sensor is ready
            bool isReady = ph_sensor_->is_sensor_ready();

            // Add calibration status if available
            // Note: Calibration query would require async call, so we just show ready status
            if (isReady) {
                status = "Ready";
            } else {
                status = "Not responding";
            }

            uartDevices.push_back(UARTDeviceInfo(
                "EZO pH Sensor",
                "TX=GPIO4, RX=GPIO5",
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
            "GPIO23",
            hasReading,  // ready when has reading
            true,        // critical for pH compensation
            status
        ));

        status_logger_.updateOneWireHardwareStatus(oneWireDevices);

        // Update water temperature in sensor data section
        status_logger_.updateWaterTemperature(temp, hasReading);

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
                calendar_manager_->is_safe_mode()
            );
        }

        status_logger_.logStatus();

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
    }

    // ========================================================================
    // Time-based Light Control
    // ========================================================================
    // Check if we should update grow light based on calendar schedule
    // Only run when system is operational (not in SHUTDOWN, PAUSE, or ERROR)
    if (calendar_manager_ && time_source_ &&
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
            if (first_run || previous_light_state != should_be_on) {
                if (should_be_on) {
                    ESP_LOGI(TAG, "Grow light schedule: Turning ON (Day %d, Schedule: %02d:%02d ON / %02d:%02d OFF)",
                             schedule.day_number,
                             schedule.light_on_time / 60, schedule.light_on_time % 60,
                             schedule.light_off_time / 60, schedule.light_off_time % 60);
                    hal_->setPump("GrowLight", true);
                } else {
                    ESP_LOGI(TAG, "Grow light schedule: Turning OFF (Day %d, Schedule: %02d:%02d ON / %02d:%02d OFF)",
                             schedule.day_number,
                             schedule.light_on_time / 60, schedule.light_on_time % 60,
                             schedule.light_off_time / 60, schedule.light_off_time % 60);
                    hal_->setPump("GrowLight", false);
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
                    }
                } else {
                    ESP_LOGD(TAG, "PSM event is empty (cleared) - no restoration needed");
                }
            } else {
                ESP_LOGI(TAG, "No PSM event found - clean boot");
            }
        }
    }

    if (elapsed >= INIT_DURATION) {
        ESP_LOGI(TAG, "Boot sequence complete");

        // Check if we need to restore a persisted state from PSM
        if (boot_restore_pending_) {
            boot_restore_pending_ = false;

            if (boot_restore_state_ == ControllerState::SHUTDOWN) {
                ESP_LOGW(TAG, "RECOVERY: Restoring SHUTDOWN state from power loss");
                transitionTo(ControllerState::SHUTDOWN);
            } else if (boot_restore_state_ == ControllerState::PAUSE) {
                ESP_LOGW(TAG, "RECOVERY: Restoring PAUSE state from power loss");
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

            ESP_LOGI(TAG, "========================================================");
            ESP_LOGI(TAG, "  AUTOMATIC pH CHECK (time-based: every %u hours)",
                     ph_interval_ms / 3600000);
            ESP_LOGI(TAG, "========================================================");

            transitionTo(ControllerState::PH_PROCESSING);
            return;  // Exit IDLE immediately
        }
    } else {
        // FALLBACK: millis()-based scheduling (no time source available)
        if (now - last_ph_check_time_ >= ph_interval_ms) {
            last_ph_check_time_ = now;

            ESP_LOGW(TAG, "========================================================");
            ESP_LOGW(TAG, "  AUTOMATIC pH CHECK (fallback mode: no time source)");
            ESP_LOGW(TAG, "  Using boot-time based interval: every %u hours", ph_interval_ms / 3600000);
            ESP_LOGW(TAG, "========================================================");

            transitionTo(ControllerState::PH_PROCESSING);
            return;  // Exit IDLE immediately
        }
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

    // Future: Check for other scheduled tasks, sensor thresholds, etc.
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
            status_logger_.resolveAlert("HARDWARE_WATER_SENSOR_LOW");
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

void PlantOSController::handlePhMeasuring() {
    // Yellow pulse - stabilizing pH reading (5 minutes)
    // All pumps OFF for accurate measurement

    uint32_t elapsed = getStateElapsed();

    // On entry: Turn off all pumps for stabilization (only once!)
    if (!state_entry_executed_) {
        state_entry_executed_ = true;
        turnOffAllPumps();
        ESP_LOGI(TAG, "pH measuring: All pumps OFF for 5-minute stabilization");
        ph_readings_.clear(); // Reset readings buffer

        // Send temperature compensation before starting pH measurements
        sendTemperatureCompensation();

        return;
    }

    // Take pH readings every 5 seconds
    uint32_t reading_interval = elapsed / 5000; // Every 5 seconds
    if (reading_interval > state_counter_) {
        state_counter_ = reading_interval;

        if (hasPhValue()) {
            // Sensor has value - check if recovering from failures
            if (ENHANCED_ERROR_HANDLING_ENABLED && sensor_retry_state_.consecutive_failures > 0) {
                ESP_LOGI(TAG, "pH sensor recovered after %u failures", sensor_retry_state_.consecutive_failures);
                sensor_retry_state_.reset();
                status_logger_.resolveAlert("PH_SENSOR_CRITICAL");
            }

            float ph = readPH();
            ph_readings_.push_back(ph);
            ESP_LOGI(TAG, "pH reading #%d: %.2f", ph_readings_.size(), ph);

            // Check for critical pH (out of safe range 5.0-7.5)
            if (ph < 5.0f || ph > 7.5f) {
                ESP_LOGE(TAG, "CRITICAL pH DETECTED: %.2f (safe range: 5.0-7.5)", ph);

                // Log critical pH event to PSM for persistence
                if (psm_) {
                    // Only log once per measurement cycle to avoid spam
                    if (ph_readings_.size() == 1) {
                        psm_->logEvent("PH_CRITICAL", 2);  // 2 = ERROR status
                        ESP_LOGW(TAG, "Critical pH event logged to PSM for recovery tracking");
                    }
                }

                // Add alert to status logger
                std::string alert_msg = "pH outside safe range: " + std::to_string(ph) + " (safe: 5.0-7.5)";
                status_logger_.updateAlertStatus("PH_CRITICAL", alert_msg);
            } else {
                // pH back in safe range - clear critical event if it exists
                if (psm_ && ph_readings_.size() == 1) {
                    // Check if there's a PH_CRITICAL event and clear it
                    if (psm_->hasEvent()) {
                        esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
                        std::string eventID(event.eventID);
                        if (eventID == "PH_CRITICAL") {
                            psm_->clearEvent();
                            status_logger_.clearAlert("PH_CRITICAL");
                            ESP_LOGI(TAG, "pH returned to safe range - cleared PH_CRITICAL event");
                        }
                    }
                }
            }
        } else {
            // Sensor has no value - implement retry logic with exponential backoff
            if (ENHANCED_ERROR_HANDLING_ENABLED) {
                sensor_retry_state_.recordFailure();

                if (!sensor_retry_state_.shouldRetry()) {
                    // Max retries exceeded - abort with comprehensive error
                    ESP_LOGE(TAG, "pH sensor not responding after %u attempts - aborting measurement",
                             sensor_retry_state_.consecutive_failures);

                    status_logger_.updateAlertWithContext(
                        "PH_SENSOR_CRITICAL",
                        "pH sensor not responding after " + std::to_string(sensor_retry_state_.consecutive_failures) + " attempts",
                        "Sensor has no value: hasPhValue() returned false for " +
                            std::to_string(sensor_retry_state_.consecutive_failures) + " consecutive readings",
                        "Check sensor wiring (UART TX=GPIO20, RX=GPIO21), verify sensor power (5V), try sensor calibration from web UI",
                        "pH measurement phase, " + std::to_string(ph_readings_.size()) +
                            " readings collected before failure",
                        "Aborting to IDLE. System will retry pH correction on next cycle.",
                        0
                    );

                    sensor_retry_state_.reset();

                    // Clear any PH_CRITICAL event since measurement failed
                    if (psm_ && psm_->hasEvent()) {
                        esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
                        std::string eventID(event.eventID);
                        if (eventID == "PH_CRITICAL") {
                            psm_->clearEvent();
                        }
                    }

                    transitionTo(ControllerState::IDLE);
                    return;
                }

                // Check if backoff period is complete
                uint32_t current_time = hal_ ? hal_->getSystemTime() : 0;
                if (!sensor_retry_state_.isBackoffComplete(current_time)) {
                    // Still in backoff - wait
                    uint32_t wait_time = (current_time - sensor_retry_state_.last_failure_time) / 1000;
                    uint32_t backoff_sec = sensor_retry_state_.backoff_delay_ms / 1000;
                    ESP_LOGD(TAG, "Sensor backoff: waiting %u/%u seconds",
                             wait_time, backoff_sec);
                    return;
                }

                // Backoff complete - ready for retry
                ESP_LOGW(TAG, "pH sensor retry attempt %u/%u (backoff: %u ms)",
                         sensor_retry_state_.consecutive_failures,
                         sensor_retry_state_.MAX_SENSOR_RETRIES,
                         sensor_retry_state_.backoff_delay_ms);
            } else {
                // Legacy behavior - just log warning
                ESP_LOGW(TAG, "pH sensor has no value - waiting for reading");
            }
        }
    }

    // Wait full 5 minutes before calculating
    if (elapsed < PH_MEASURING_DURATION) {
        return;
    }

    // Measurement period complete - calculate robust average
    if (ph_readings_.empty()) {
        ESP_LOGE(TAG, "No pH readings collected - aborting correction");

        // Set comprehensive alert before ERROR transition
        status_logger_.updateAlertWithContext(
            "NO_PH_READINGS",
            "No pH readings collected during 5-minute measurement phase",
            "Sensor returned no values: hasPhValue() was false for entire measurement period",
            "Check pH sensor UART connection (TX=GPIO20, RX=GPIO21), verify sensor power (5V), try sensor calibration from web UI",
            "pH correction aborted during measurement phase after 5 minutes",
            "System will return to IDLE and can retry on next cycle",
            0
        );

        // Clear any PH_CRITICAL event since measurement failed
        if (psm_ && psm_->hasEvent()) {
            esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
            std::string eventID(event.eventID);
            if (eventID == "PH_CRITICAL") {
                psm_->clearEvent();
            }
        }

        transitionTo(ControllerState::ERROR);
        return;
    }

    ph_current_ = calculateRobustPhAverage();
    ESP_LOGI(TAG, "pH stabilization complete. Robust average: %.2f (from %d readings)",
             ph_current_, ph_readings_.size());

    transitionTo(ControllerState::PH_CALCULATING);
}

void PlantOSController::handlePhCalculating() {
    // Yellow fast blink - determining pH adjustment
    // Decision point: Does pH need correction?

    // Get pH target range from calendar schedule for current grow day
    float target_min = PH_TARGET_MIN; // Fallback: 5.5
    float target_max = PH_TARGET_MAX; // Fallback: 6.5

    if (calendar_manager_) {
        // Use calendar manager's actual current day (not timestamp-based calculation)
        uint8_t current_day = calendar_manager_->get_current_day();
        auto schedule = calendar_manager_->get_schedule(current_day);
        target_min = schedule.target_ph_min;
        target_max = schedule.target_ph_max;
        ESP_LOGI(TAG, "pH: %.2f, Target (day %d): %.2f-%.2f",
                 ph_current_, current_day, target_min, target_max);
    } else {
        ESP_LOGI(TAG, "pH: %.2f, Target (default): %.2f-%.2f", ph_current_, target_min, target_max);
    }

    // Check if pH is within target range
    if (ph_current_ >= target_min && ph_current_ <= target_max) {
        ESP_LOGI(TAG, "pH within target range - no correction needed");

        // Record successful completion of pH correction operation
        recordOperationStep("pH_CORRECTION_SUCCESS");

        // Resolve any active pH-related alerts
        if (ENHANCED_ERROR_HANDLING_ENABLED) {
            status_logger_.resolveAlert("PH_CRITICAL");
            status_logger_.resolveAlert("NO_PH_READINGS");
            status_logger_.resolveAlert("PUMP_REJECTION_ACID");
            status_logger_.resolveAlert("PH_SENSOR_CRITICAL");
        }

        // Clear PSM event - pH correction complete
        if (psm_) {
            psm_->clearEvent();
        }

        transitionTo(ControllerState::IDLE);
        return;
    }

    // Check if pH is too low (cannot correct - no base pump)
    if (ph_current_ < target_min) {
        ESP_LOGW(TAG, "pH too low (%.2f) - cannot correct without base pump", ph_current_);
        transitionTo(ControllerState::IDLE);
        return;
    }

    // pH is too high - calculate required acid dose in mL
    float dose_ml = calculateAcidDoseML(ph_current_, target_max);

    // Check minimum dose threshold (avoid tiny corrections)
    if (dose_ml < 0.5f) {
        ESP_LOGI(TAG, "Calculated dose too small (%.1f mL < 0.5 mL) - skipping correction", dose_ml);
        transitionTo(ControllerState::IDLE);
        return;
    }

    // Convert mL to seconds using HAL pumpflow
    float dose_seconds = hal_->pumpflow("AcidPump", dose_ml);
    uint32_t dose_ms = static_cast<uint32_t>(dose_seconds * 1000.0f);

    // Check max attempts to prevent infinite loops
    if (ph_attempt_count_ >= MAX_PH_ATTEMPTS) {
        ESP_LOGE(TAG, "Max pH correction attempts reached (%d) - aborting", MAX_PH_ATTEMPTS);
        // Phase 7: Update alert status here
        transitionTo(ControllerState::IDLE);
        return;
    }

    // Proceed with injection
    ph_dose_ml_ = dose_ml;
    ph_dose_duration_ms_ = dose_ms;
    ph_attempt_count_++;

    ESP_LOGI(TAG, "pH correction needed: %.2f → %.2f (dose: %.1f mL = %.2f sec, attempt %d/%d)",
             ph_current_, target_max, dose_ml, dose_seconds, ph_attempt_count_, MAX_PH_ATTEMPTS);

    transitionTo(ControllerState::PH_INJECTING);
}

void PlantOSController::handlePhInjecting() {
    // Cyan pulse - acid dosing in progress
    // Air pump + Acid pump active (or acid pump only if no air pump)

    uint32_t elapsed = getStateElapsed();

    // On entry: Activate pumps (only once!)
    if (!state_entry_executed_) {
        state_entry_executed_ = true;

        // Calculate injection duration in seconds (round up)
        uint32_t injection_sec = (ph_dose_duration_ms_ + 999) / 1000;

        // Calculate mixing duration (same as handlePhMixing does)
        uint32_t mixing_duration_ms = calculatePhMixingDuration();
        uint32_t mixing_sec = (mixing_duration_ms + 999) / 1000;

        // Total AirPump ON time = injection + mixing
        uint32_t total_air_pump_sec = injection_sec + mixing_sec;

        ESP_LOGI(TAG, "Starting acid injection: %.1f mL (%.2f sec)", ph_dose_ml_, injection_sec / 1.0f);
        ESP_LOGI(TAG, "AirPump pattern: %u sec (injection) + %u sec (mixing) = %u sec total",
                 injection_sec, mixing_sec, total_air_pump_sec);

        // Send AirPump pattern via Shelly sequence API (single HTTP call for entire pH correction)
        // Pattern: single ON duration covering injection+mixing, finalstate=ON (for IDLE mode)
        if (hal_) {
            std::vector<uint32_t> pattern = {total_air_pump_sec};

            // Validate pattern through SafetyGate if available
            if (safety_gate_ && !safety_gate_->validateAirPumpPattern(pattern)) {
                ESP_LOGW(TAG, "AirPump pattern rejected by SafetyGate - using simple ON command");
                // Fall back to simple ON (no duration limit)
                requestPump(AIR_PUMP, true, 0);
            } else {
                // Send pattern to Shelly: ON for total duration, then stay ON (finalstate=1 for IDLE)
                hal_->setAirPumpPattern(pattern, true);
                ESP_LOGI(TAG, "AirPump Shelly pattern sent: %u sec ON, then stay ON", total_air_pump_sec);
            }
        } else {
            ESP_LOGD(TAG, "HAL not configured - air pump control skipped");
        }

        // Request acid pump via SafetyGate with adaptive duration (will auto-adapt if needed)
        if (!requestPumpAdaptive(ACID_PUMP, true, injection_sec, false)) {
            ESP_LOGE(TAG, "Acid pump rejected by SafetyGate even after adaptation - aborting");
            // Stop air pump sequence since we're aborting
            if (hal_) {
                hal_->stopAirPumpSequence(true);  // Stop but keep ON for normal operation
            }

            // Set comprehensive alert before ERROR transition
            status_logger_.updateAlertWithContext(
                "PUMP_REJECTION_ACID",
                "Acid pump rejected by SafetyGate",
                "SafetyGate rejected acid pump command even after duration adaptation",
                "Check acid pump wiring, verify pump not stuck, check SafetyGate max duration config",
                "pH correction attempt " + std::to_string(ph_attempt_count_ + 1) + "/" +
                    std::to_string(MAX_PH_ATTEMPTS) + ", injection phase",
                "Aborting pH correction. Will retry on next cycle.",
                0
            );

            transitionTo(ControllerState::ERROR);
            return;
        }

        ESP_LOGI(TAG, "Acid pump active - dosing %.1f mL", ph_dose_ml_);

        // CRITICAL FIX: Reset timer to start from NOW (after HTTP delays)
        // The AirPump HTTP request is async, but we reset timer for consistency.
        state_start_time_ = hal_->getSystemTime();
        ESP_LOGD(TAG, "Timer reset: injection duration starts now");

        return;
    }

    // Wait for injection duration + 200ms safety margin
    uint32_t total_duration = ph_dose_duration_ms_ + 200;

    if (elapsed >= total_duration) {
        // Injection complete - explicitly stop acid pump
        requestPump(ACID_PUMP, false);

        ESP_LOGI(TAG, "Acid dosing complete: %.1f mL added - starting mixing phase", ph_dose_ml_);
        transitionTo(ControllerState::PH_MIXING);
    }
}

void PlantOSController::handlePhMixing() {
    // Blue pulse - mixing after acid injection
    // Air pump runs to distribute acid throughout tank (duration varies by tank volume)
    // NOTE: AirPump is controlled via Shelly pattern API - pattern was sent in PH_INJECTING
    uint32_t elapsed = getStateElapsed();

    // On entry: Calculate mixing duration for timer only (air pump already running via pattern)
    if (!state_entry_executed_) {
        state_entry_executed_ = true;

        // Calculate mixing duration based on tank volume (linear: 1.7L→30s, 70L→120s)
        ph_mixing_duration_ms_ = calculatePhMixingDuration();

        ESP_LOGI(TAG, "Mixing phase: waiting %.1f seconds (air pump controlled by Shelly pattern)",
                 ph_mixing_duration_ms_ / 1000.0f);

        // No air pump commands here - Shelly pattern handles the full injection+mixing duration
    }

    if (elapsed >= ph_mixing_duration_ms_) {
        ESP_LOGI(TAG, "pH mixing complete (%.1f seconds)", ph_mixing_duration_ms_ / 1000.0f);

        // Check if cycling was enabled before pH correction
        // If so, restore the cycling pattern on Shelly
        if (safety_gate_ && safety_gate_->isCyclingEnabled(AIR_PUMP)) {
            ESP_LOGI(TAG, "Restoring AirPump cycling pattern");
            safety_gate_->enableCycling(AIR_PUMP, true);  // Re-sends pattern to Shelly
        }
        // If cycling not enabled, the pattern's finalstate=ON keeps pump ON (Normal mode)

        // Loop back to PH_MEASURING to verify correction
        // Will continue until pH is in range OR max attempts reached
        ESP_LOGI(TAG, "Re-measuring pH to verify correction (attempt %d/%d)",
                 ph_attempt_count_, MAX_PH_ATTEMPTS);

        transitionTo(ControllerState::PH_MEASURING);
    }
}

void PlantOSController::handlePhCalibrating() {
    // Yellow fast blink - pH sensor calibration with robust averaging
    // 3-point calibration sequence: Mid (7.00) → Low (4.00) → High (10.01)
    // Each point waits for stable readings before proceeding

    if (!ph_sensor_ || !hal_) {
        ESP_LOGE(TAG, "pH sensor or HAL not configured - cannot calibrate");
        transitionTo(ControllerState::ERROR);
        return;
    }

    // Check if sensor hardware is actually connected and responding
    if (!ph_sensor_->is_sensor_ready()) {
        ESP_LOGE(TAG, "pH sensor hardware not responding - calibration aborted");
        ESP_LOGE(TAG, "Check UART connection and power to sensor");

        // Set comprehensive alert before ERROR transition
        status_logger_.updateAlertWithContext(
            "PH_SENSOR_HARDWARE_FAILURE",
            "pH sensor hardware not responding",
            "Sensor readiness check failed: is_sensor_ready() returned false",
            "Check UART connection (TX=GPIO20, RX=GPIO21), verify sensor power (5V), inspect sensor LED status",
            "Calibration aborted during initialization",
            "Fix hardware connection and retry calibration from web UI",
            0
        );

        // Ensure verbose mode is off even on error
        if (ph_sensor_) {
            ph_sensor_->set_verbose(false);
        }
        transitionTo(ControllerState::ERROR);
        return;
    }

    uint32_t now = esphome::millis();
    uint32_t elapsed = now - calib_step_start_time_;

    switch (calib_step_) {
        // ====================================================================
        // MID-POINT CALIBRATION (pH 7.00)
        // ====================================================================
        case CalibrationStep::MID_PROMPT:
            if (elapsed < 100) {  // Log prompt once at start
                ESP_LOGI(TAG, "");
                ESP_LOGI(TAG, "===========================================================");
                ESP_LOGI(TAG, "  STEP 1/3: MID-POINT CALIBRATION (pH 7.00)");
                ESP_LOGI(TAG, "===========================================================");
                ESP_LOGI(TAG, "1. Insert pH probe into pH 7.00 buffer solution");
                ESP_LOGI(TAG, "2. Waiting %d seconds for stability check...", CALIB_PROMPT_DURATION / 1000);
                ESP_LOGI(TAG, "");
            }

            if (elapsed >= CALIB_PROMPT_DURATION) {
                ESP_LOGI(TAG, "Starting stability check for mid-point calibration...");

                // Send temperature compensation before calibration readings
                sendTemperatureCompensation();

                resetCalibrationBatch();
                calib_step_ = CalibrationStep::MID_STABILIZING;
                calib_step_start_time_ = now;
            }
            break;

        case CalibrationStep::MID_STABILIZING:
            // Take readings (20 per batch, 1/second, 5 batches with 30s wait between)
            if (calib_readings_in_batch_ < CALIB_READINGS_PER_BATCH) {
                // Log when starting a new batch
                if (calib_readings_in_batch_ == 0 && calib_current_batch_ < CALIB_TOTAL_BATCHES) {
                    // Only log once when starting the batch (avoid spamming)
                    static int last_logged_batch = -1;
                    if (last_logged_batch != calib_current_batch_) {
                        ESP_LOGI(TAG, "MID: Starting batch %d/%d - Taking %d readings at 1-second intervals",
                                calib_current_batch_ + 1, CALIB_TOTAL_BATCHES, CALIB_READINGS_PER_BATCH);
                        last_logged_batch = calib_current_batch_;
                    }
                }

                // Time to take next reading?
                if (now - calib_last_reading_time_ >= CALIB_READING_INTERVAL) {
                    float ph_value;
                    if (hal_->takeSinglePhReading(ph_value)) {
                        calib_batch_sum_ += ph_value;
                        calib_readings_in_batch_++;
                        calib_last_reading_time_ = now;
                        ESP_LOGD(TAG, "MID: Batch %d, Reading %d/%d: pH %.2f",
                                calib_current_batch_ + 1, calib_readings_in_batch_,
                                CALIB_READINGS_PER_BATCH, ph_value);
                    } else {
                        ESP_LOGW(TAG, "Failed to take pH reading - retrying");
                    }
                }
            } else {
                // Batch complete - calculate average
                float batch_avg = calib_batch_sum_ / CALIB_READINGS_PER_BATCH;
                calib_batch_averages_[calib_current_batch_] = batch_avg;
                ESP_LOGI(TAG, "MID: Batch %d/%d complete - Average: pH %.2f (Total readings: %d)",
                        calib_current_batch_ + 1, CALIB_TOTAL_BATCHES, batch_avg,
                        (calib_current_batch_ + 1) * CALIB_READINGS_PER_BATCH);

                calib_current_batch_++;

                if (calib_current_batch_ >= CALIB_TOTAL_BATCHES) {
                    // All batches complete - check stability
                    if (checkCalibrationStability()) {
                        ESP_LOGI(TAG, "MID: Readings are STABLE - proceeding to calibration");
                        calib_step_ = CalibrationStep::MID_CALIBRATE;
                        calib_step_start_time_ = now;
                    } else {
                        ESP_LOGW(TAG, "MID: Readings NOT stable - restarting stability check");
                        resetCalibrationBatch();
                    }
                } else {
                    // Wait 30 seconds before next batch
                    ESP_LOGI(TAG, "MID: Waiting %d seconds before next batch...", CALIB_BATCH_WAIT / 1000);
                    calib_batch_complete_time_ = now;
                    calib_readings_in_batch_ = 0;  // Reset for next batch
                    calib_batch_sum_ = 0.0f;
                }
            }

            // Handle wait between batches
            if (calib_readings_in_batch_ == 0 && calib_current_batch_ > 0 &&
                calib_current_batch_ < CALIB_TOTAL_BATCHES) {
                if (now - calib_batch_complete_time_ < CALIB_BATCH_WAIT) {
                    // Still waiting
                    return;
                }
            }
            break;

        case CalibrationStep::MID_CALIBRATE:
            ESP_LOGI(TAG, "Sending mid-point calibration command at pH 7.00...");
            if (!hal_->startPhCalibration(7.00f, 0)) {  // 0 = mid
                ESP_LOGE(TAG, "MID: Calibration command FAILED");

                // Set comprehensive alert before ERROR transition
                status_logger_.updateAlertWithContext(
                    "CALIBRATION_FAILED_MID",
                    "Mid-point calibration command failed (pH 7.00)",
                    "HAL calibration command returned false for mid-point (pH 7.00)",
                    "Verify pH sensor in pH 7.00 buffer solution, check sensor stability, retry calibration",
                    "3-point calibration: Step 1 of 3 (mid-point) failed",
                    "Calibration aborted. Verify buffer solution and sensor condition, then retry.",
                    0
                );

                // Ensure verbose mode is off even on error
                if (ph_sensor_) {
                    ph_sensor_->set_verbose(false);
                }
                transitionTo(ControllerState::ERROR);
                return;
            } else {
                ESP_LOGI(TAG, "✓ MID: Calibration command sent successfully");
            }
            calib_step_ = CalibrationStep::MID_COMPLETE;
            calib_step_start_time_ = now;
            break;

        case CalibrationStep::MID_COMPLETE:
            if (elapsed >= 1000) {  // Wait 1s for command to complete
                ESP_LOGI(TAG, "MID: Mid-point calibration complete!");
                calib_step_ = CalibrationStep::LOW_PROMPT;
                calib_step_start_time_ = now;
            }
            break;

        // ====================================================================
        // LOW-POINT CALIBRATION (pH 4.00)
        // ====================================================================
        case CalibrationStep::LOW_PROMPT:
            if (elapsed < 100) {
                ESP_LOGI(TAG, "");
                ESP_LOGI(TAG, "===========================================================");
                ESP_LOGI(TAG, "  STEP 2/3: LOW-POINT CALIBRATION (pH 4.00)");
                ESP_LOGI(TAG, "===========================================================");
                ESP_LOGI(TAG, "1. Remove probe and rinse with distilled water");
                ESP_LOGI(TAG, "2. Insert pH probe into pH 4.00 buffer solution");
                ESP_LOGI(TAG, "3. Waiting %d seconds for stability check...", CALIB_PROMPT_DURATION / 1000);
                ESP_LOGI(TAG, "");
            }

            if (elapsed >= CALIB_PROMPT_DURATION) {
                ESP_LOGI(TAG, "Starting stability check for low-point calibration...");

                // Send temperature compensation before calibration readings
                sendTemperatureCompensation();

                resetCalibrationBatch();
                calib_step_ = CalibrationStep::LOW_STABILIZING;
                calib_step_start_time_ = now;
            }
            break;

        case CalibrationStep::LOW_STABILIZING:
            // Same logic as MID_STABILIZING
            if (calib_readings_in_batch_ < CALIB_READINGS_PER_BATCH) {
                // Log when starting a new batch
                if (calib_readings_in_batch_ == 0 && calib_current_batch_ < CALIB_TOTAL_BATCHES) {
                    // Only log once when starting the batch (avoid spamming)
                    static int last_logged_batch = -1;
                    if (last_logged_batch != calib_current_batch_) {
                        ESP_LOGI(TAG, "LOW: Starting batch %d/%d - Taking %d readings at 1-second intervals",
                                calib_current_batch_ + 1, CALIB_TOTAL_BATCHES, CALIB_READINGS_PER_BATCH);
                        last_logged_batch = calib_current_batch_;
                    }
                }

                if (now - calib_last_reading_time_ >= CALIB_READING_INTERVAL) {
                    float ph_value;
                    if (hal_->takeSinglePhReading(ph_value)) {
                        calib_batch_sum_ += ph_value;
                        calib_readings_in_batch_++;
                        calib_last_reading_time_ = now;
                        ESP_LOGD(TAG, "LOW: Batch %d, Reading %d/%d: pH %.2f",
                                calib_current_batch_ + 1, calib_readings_in_batch_,
                                CALIB_READINGS_PER_BATCH, ph_value);
                    }
                }
            } else {
                float batch_avg = calib_batch_sum_ / CALIB_READINGS_PER_BATCH;
                calib_batch_averages_[calib_current_batch_] = batch_avg;
                ESP_LOGI(TAG, "LOW: Batch %d/%d complete - Average: pH %.2f (Total readings: %d)",
                        calib_current_batch_ + 1, CALIB_TOTAL_BATCHES, batch_avg,
                        (calib_current_batch_ + 1) * CALIB_READINGS_PER_BATCH);

                calib_current_batch_++;

                if (calib_current_batch_ >= CALIB_TOTAL_BATCHES) {
                    if (checkCalibrationStability()) {
                        ESP_LOGI(TAG, "LOW: Readings are STABLE - proceeding to calibration");
                        calib_step_ = CalibrationStep::LOW_CALIBRATE;
                        calib_step_start_time_ = now;
                    } else {
                        ESP_LOGW(TAG, "LOW: Readings NOT stable - restarting stability check");
                        resetCalibrationBatch();
                    }
                } else {
                    ESP_LOGI(TAG, "LOW: Waiting %d seconds before next batch...", CALIB_BATCH_WAIT / 1000);
                    calib_batch_complete_time_ = now;
                    calib_readings_in_batch_ = 0;
                    calib_batch_sum_ = 0.0f;
                }
            }

            if (calib_readings_in_batch_ == 0 && calib_current_batch_ > 0 &&
                calib_current_batch_ < CALIB_TOTAL_BATCHES) {
                if (now - calib_batch_complete_time_ < CALIB_BATCH_WAIT) {
                    return;
                }
            }
            break;

        case CalibrationStep::LOW_CALIBRATE:
            ESP_LOGI(TAG, "Sending low-point calibration command at pH 4.00...");
            if (!hal_->startPhCalibration(4.00f, 1)) {  // 1 = low
                ESP_LOGE(TAG, "LOW: Calibration command FAILED");

                // Set comprehensive alert before ERROR transition
                status_logger_.updateAlertWithContext(
                    "CALIBRATION_FAILED_LOW",
                    "Low-point calibration command failed (pH 4.00)",
                    "HAL calibration command returned false for low-point (pH 4.00)",
                    "Verify pH sensor in pH 4.00 buffer solution, check sensor stability, retry calibration",
                    "3-point calibration: Step 2 of 3 (low-point) failed",
                    "Calibration aborted. Verify buffer solution and sensor condition, then retry.",
                    0
                );

                // Ensure verbose mode is off even on error
                if (ph_sensor_) {
                    ph_sensor_->set_verbose(false);
                }
                transitionTo(ControllerState::ERROR);
                return;
            } else {
                ESP_LOGI(TAG, "✓ LOW: Calibration command sent successfully");
            }
            calib_step_ = CalibrationStep::LOW_COMPLETE;
            calib_step_start_time_ = now;
            break;

        case CalibrationStep::LOW_COMPLETE:
            if (elapsed >= 1000) {
                ESP_LOGI(TAG, "LOW: Low-point calibration complete!");
                calib_step_ = CalibrationStep::HIGH_PROMPT;
                calib_step_start_time_ = now;
            }
            break;

        // ====================================================================
        // HIGH-POINT CALIBRATION (pH 10.01)
        // ====================================================================
        case CalibrationStep::HIGH_PROMPT:
            if (elapsed < 100) {
                ESP_LOGI(TAG, "");
                ESP_LOGI(TAG, "===========================================================");
                ESP_LOGI(TAG, "  STEP 3/3: HIGH-POINT CALIBRATION (pH 10.01)");
                ESP_LOGI(TAG, "===========================================================");
                ESP_LOGI(TAG, "1. Remove probe and rinse with distilled water");
                ESP_LOGI(TAG, "2. Insert pH probe into pH 10.01 buffer solution");
                ESP_LOGI(TAG, "3. Waiting %d seconds for stability check...", CALIB_PROMPT_DURATION / 1000);
                ESP_LOGI(TAG, "");
            }

            if (elapsed >= CALIB_PROMPT_DURATION) {
                ESP_LOGI(TAG, "Starting stability check for high-point calibration...");

                // Send temperature compensation before calibration readings
                sendTemperatureCompensation();

                resetCalibrationBatch();
                calib_step_ = CalibrationStep::HIGH_STABILIZING;
                calib_step_start_time_ = now;
            }
            break;

        case CalibrationStep::HIGH_STABILIZING:
            // Same logic as MID_STABILIZING and LOW_STABILIZING
            if (calib_readings_in_batch_ < CALIB_READINGS_PER_BATCH) {
                // Log when starting a new batch
                if (calib_readings_in_batch_ == 0 && calib_current_batch_ < CALIB_TOTAL_BATCHES) {
                    // Only log once when starting the batch (avoid spamming)
                    static int last_logged_batch = -1;
                    if (last_logged_batch != calib_current_batch_) {
                        ESP_LOGI(TAG, "HIGH: Starting batch %d/%d - Taking %d readings at 1-second intervals",
                                calib_current_batch_ + 1, CALIB_TOTAL_BATCHES, CALIB_READINGS_PER_BATCH);
                        last_logged_batch = calib_current_batch_;
                    }
                }

                if (now - calib_last_reading_time_ >= CALIB_READING_INTERVAL) {
                    float ph_value;
                    if (hal_->takeSinglePhReading(ph_value)) {
                        calib_batch_sum_ += ph_value;
                        calib_readings_in_batch_++;
                        calib_last_reading_time_ = now;
                        ESP_LOGD(TAG, "HIGH: Batch %d, Reading %d/%d: pH %.2f",
                                calib_current_batch_ + 1, calib_readings_in_batch_,
                                CALIB_READINGS_PER_BATCH, ph_value);
                    }
                }
            } else {
                float batch_avg = calib_batch_sum_ / CALIB_READINGS_PER_BATCH;
                calib_batch_averages_[calib_current_batch_] = batch_avg;
                ESP_LOGI(TAG, "HIGH: Batch %d/%d complete - Average: pH %.2f (Total readings: %d)",
                        calib_current_batch_ + 1, CALIB_TOTAL_BATCHES, batch_avg,
                        (calib_current_batch_ + 1) * CALIB_READINGS_PER_BATCH);

                calib_current_batch_++;

                if (calib_current_batch_ >= CALIB_TOTAL_BATCHES) {
                    if (checkCalibrationStability()) {
                        ESP_LOGI(TAG, "HIGH: Readings are STABLE - proceeding to calibration");
                        calib_step_ = CalibrationStep::HIGH_CALIBRATE;
                        calib_step_start_time_ = now;
                    } else {
                        ESP_LOGW(TAG, "HIGH: Readings NOT stable - restarting stability check");
                        resetCalibrationBatch();
                    }
                } else {
                    ESP_LOGI(TAG, "HIGH: Waiting %d seconds before next batch...", CALIB_BATCH_WAIT / 1000);
                    calib_batch_complete_time_ = now;
                    calib_readings_in_batch_ = 0;
                    calib_batch_sum_ = 0.0f;
                }
            }

            if (calib_readings_in_batch_ == 0 && calib_current_batch_ > 0 &&
                calib_current_batch_ < CALIB_TOTAL_BATCHES) {
                if (now - calib_batch_complete_time_ < CALIB_BATCH_WAIT) {
                    return;
                }
            }
            break;

        case CalibrationStep::HIGH_CALIBRATE:
            ESP_LOGI(TAG, "Sending high-point calibration command at pH 10.01...");
            if (!hal_->startPhCalibration(10.01f, 2)) {  // 2 = high
                ESP_LOGE(TAG, "HIGH: Calibration command FAILED");

                // Set comprehensive alert before ERROR transition
                status_logger_.updateAlertWithContext(
                    "CALIBRATION_FAILED_HIGH",
                    "High-point calibration command failed (pH 10.01)",
                    "HAL calibration command returned false for high-point (pH 10.01)",
                    "Verify pH sensor in pH 10.01 buffer solution, check sensor stability, retry calibration",
                    "3-point calibration: Step 3 of 3 (high-point) failed",
                    "Calibration aborted. Verify buffer solution and sensor condition, then retry.",
                    0
                );

                // Ensure verbose mode is off even on error
                if (ph_sensor_) {
                    ph_sensor_->set_verbose(false);
                }
                transitionTo(ControllerState::ERROR);
                return;
            } else {
                ESP_LOGI(TAG, "✓ HIGH: Calibration command sent successfully");
            }
            calib_step_ = CalibrationStep::HIGH_COMPLETE;
            calib_step_start_time_ = now;
            break;

        case CalibrationStep::HIGH_COMPLETE:
            if (elapsed >= 1000) {
                ESP_LOGI(TAG, "HIGH: High-point calibration complete!");
                calib_step_ = CalibrationStep::COMPLETE;
                calib_step_start_time_ = now;
            }
            break;

        // ====================================================================
        // CALIBRATION COMPLETE
        // ====================================================================
        case CalibrationStep::COMPLETE:
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "===========================================================");
            ESP_LOGI(TAG, "  3-POINT pH CALIBRATION COMPLETE!");
            ESP_LOGI(TAG, "===========================================================");
            ESP_LOGI(TAG, "Remove probe from buffer solution");
            ESP_LOGI(TAG, "Rinse with distilled water and return to tank");
            ESP_LOGI(TAG, "System returning to IDLE state...");
            ESP_LOGI(TAG, "");

            // Disable verbose mode - calibration complete
            if (ph_sensor_) {
                ph_sensor_->set_verbose(false);
                ESP_LOGI(TAG, "Verbose mode DISABLED - returning to normal operation");
            }

            // Query calibration status to verify
            ph_sensor_->query_calibration_status();

            // Clear any calibration failure alerts from previous attempts
            status_logger_.resolveAlert("CALIBRATION_FAILED_MID");
            status_logger_.resolveAlert("CALIBRATION_FAILED_LOW");
            status_logger_.resolveAlert("CALIBRATION_FAILED_HIGH");

            // Clear PSM event
            if (psm_) {
                psm_->clearEvent();
            }

            // Reset calibration state
            calib_step_ = CalibrationStep::IDLE;

            // Return to IDLE
            transitionTo(ControllerState::IDLE);
            break;

        case CalibrationStep::IDLE:
        default:
            ESP_LOGW(TAG, "Unexpected calibration step");
            transitionTo(ControllerState::IDLE);
            break;
    }
}

void PlantOSController::handleFeeding() {
    // Orange pulse LED handled by LedBehaviorSystem
    // Sequential nutrient pump activation: A → B → C

    uint32_t elapsed = getStateElapsed();

    // Static variables to cache doses and duration across loop iterations
    static float dose_a_ml = 0.0f;
    static float dose_b_ml = 0.0f;
    static float dose_c_ml = 0.0f;
    static uint32_t current_duration_ms = 0;

    // On first entry to feeding state: calculate doses and send temperature compensation
    if (state_counter_ == 0 && elapsed < 100) {
        sendTemperatureCompensation();

        // Calculate nutrient doses from CalendarManager (mL/L) and convert to actual mL
        if (calendar_manager_ && hal_) {
            // Use calendar manager's actual current day (not timestamp-based calculation)
            uint8_t current_day = calendar_manager_->get_current_day();

            // Get schedule for current day from calendar
            auto schedule = calendar_manager_->get_schedule(current_day);

            // Select tank volume based on operation context:
            // - Normal feed (auto-feed): use delta volume (LOW→HIGH)
            // - Reservoir change: use total volume (EMPTY→HIGH)
            float tank_volume_liters = is_reservoir_change_
                ? hal_->getTotalTankVolume()
                : hal_->getTankVolumeDelta();

            ESP_LOGI(TAG, "Using %s tank volume: %.1fL",
                     is_reservoir_change_ ? "TOTAL (reservoir change)" : "DELTA (normal feed)",
                     tank_volume_liters);

            // Calculate actual mL doses from mL/L concentrations
            dose_a_ml = schedule.nutrient_A_ml_per_liter * tank_volume_liters;
            dose_b_ml = schedule.nutrient_B_ml_per_liter * tank_volume_liters;
            dose_c_ml = schedule.nutrient_C_ml_per_liter * tank_volume_liters;

            ESP_LOGI(TAG, "Feeding day %d: Tank %.1fL, A:%.2f mL, B:%.2f mL, C:%.2f mL",
                     current_day, tank_volume_liters, dose_a_ml, dose_b_ml, dose_c_ml);
        } else {
            ESP_LOGW(TAG, "Calendar not available - skipping nutrient dosing");
        }
    }

    // Use state_counter to track which pump we're on (0=A, 1=B, 2=C, 3=done)
    const char* pump_name = nullptr;
    float dose_ml = 0.0f;

    if (state_counter_ == 0) {
        pump_name = NUTRIENT_PUMP_A;
        dose_ml = dose_a_ml;
    } else if (state_counter_ == 1) {
        pump_name = NUTRIENT_PUMP_B;
        dose_ml = dose_b_ml;
    } else if (state_counter_ == 2) {
        pump_name = NUTRIENT_PUMP_C;
        dose_ml = dose_c_ml;
    } else {
        // All pumps complete
        ESP_LOGI(TAG, "Feeding sequence complete");

        // Clear PSM event - feeding complete
        if (psm_) {
            psm_->clearEvent();
        }

        // Check if auto pH correction should follow
        if (auto_ph_correction_pending_) {
            ESP_LOGI(TAG, "Auto pH correction pending - starting pH check");
            auto_ph_correction_pending_ = false;

            // Reset pH correction state
            ph_attempt_count_ = 0;
            ph_readings_.clear();
            ph_current_ = 0.0f;
            ph_dose_ml_ = 0.0f;
            ph_dose_duration_ms_ = 0;

            // Log event for crash recovery
            if (psm_) {
                psm_->logEvent("PH_CORRECTION", 0);  // 0 = STARTED
            }

            transitionTo(ControllerState::PH_PROCESSING);
        } else {
            transitionTo(ControllerState::IDLE);
        }
        return;
    }

    // On entry for this pump: calculate duration and activate it (only once!)
    if (!state_entry_executed_) {
        state_entry_executed_ = true;
        // Convert mL to duration using HAL pumpflow (only once per pump)
        if (dose_ml > 0.0f && hal_) {
            float duration_sec = hal_->pumpflow(pump_name, dose_ml);
            current_duration_ms = static_cast<uint32_t>(duration_sec * 1000.0f);
            ESP_LOGI(TAG, "%s dose: %.2f mL = %.2f sec (%d ms)",
                     pump_name, dose_ml, duration_sec, current_duration_ms);
        } else {
            current_duration_ms = 0;
        }

        if (current_duration_ms > 0) {
            uint32_t duration_sec = (current_duration_ms + 999) / 1000;  // Round up to seconds

            // Request pump with adaptive duration (will auto-adapt if needed)
            if (!requestPumpAdaptive(pump_name, true, duration_sec, false)) {
                ESP_LOGE(TAG, "%s command rejected by SafetyGate even after adaptation!", pump_name);

                // Set comprehensive alert before aborting
                std::string pump_id_str(pump_name);
                status_logger_.updateAlertWithContext(
                    "PUMP_REJECTION_" + pump_id_str,
                    std::string(pump_name) + " rejected by SafetyGate",
                    "SafetyGate rejected " + pump_id_str + " command even after duration adaptation",
                    "Check " + pump_id_str + " wiring, verify pump not stuck, check SafetyGate max duration config",
                    "Feeding sequence: dosing " + std::to_string(dose_ml) + " mL",
                    "Aborting feeding sequence. Manual intervention required.",
                    0
                );

                // Clear PSM event - feeding aborted due to safety rejection
                if (psm_) {
                    psm_->clearEvent();
                }

                // Abort feeding sequence
                transitionTo(ControllerState::IDLE);
                return;
            }

            ESP_LOGI(TAG, "%s ON for %.2f mL (%d ms)", pump_name, dose_ml, current_duration_ms);

            // Reset timer to start from NOW (prevents timing issues with any potential delays)
            // This ensures the pump duration is accurate regardless of activation delays
            state_start_time_ = hal_->getSystemTime();
            ESP_LOGD(TAG, "Timer reset: %s duration starts now", pump_name);
        } else {
            ESP_LOGI(TAG, "%s duration is 0 - skipping", pump_name);
            // Immediately move to next pump
            state_counter_++;
            state_start_time_ = hal_->getSystemTime();
        }
        return;
    }

    // Wait for duration + 200ms safety margin
    if (elapsed < current_duration_ms + 200) {
        return;
    }

    // Turn off current pump explicitly
    if (safety_gate_) {
        safety_gate_->executeCommand(pump_name, false, 0);
    }

    ESP_LOGI(TAG, "%s complete", pump_name);

    // Move to next pump
    state_counter_++;
    state_start_time_ = hal_->getSystemTime();
}

void PlantOSController::handleFeedFilling() {
    // Blue solid LED handled by LedBehaviorSystem (same as WATER_FILLING)
    // This is the first phase of the Feed operation

    uint32_t elapsed = getStateElapsed();

    // Calculate max fill duration based on tank volume and valve flow rate
    // Formula: duration = (tank_volume_mL / valve_flow_rate_mL_s) × 1.2 (20% safety margin)
    uint32_t max_fill_duration_ms = 600000;  // Default fallback: 10 minutes
    uint32_t max_fill_duration_sec = 600;    // For SafetyGate command

    if (hal_) {
        // Select tank volume based on operation context:
        // - Normal feed: fills from LOW to HIGH → use delta volume
        // - Reservoir change: fills from EMPTY to HIGH → use total volume
        float tank_volume_L = is_reservoir_change_
            ? hal_->getTotalTankVolume()
            : hal_->getTankVolumeDelta();
        float tank_volume_mL = tank_volume_L * 1000.0f;

        // Use valveflow() to calculate fill duration for the appropriate tank volume
        float fill_duration_sec = hal_->valveflow(tank_volume_mL);

        // Add 20% safety margin
        fill_duration_sec *= 1.2f;

        max_fill_duration_ms = static_cast<uint32_t>(fill_duration_sec * 1000.0f);
        max_fill_duration_sec = static_cast<uint32_t>(fill_duration_sec);

        ESP_LOGI(TAG, "[FEED_FILLING] Calculated max fill time: %.1f sec (%.1fL %s volume with 20%% margin)",
                 fill_duration_sec, tank_volume_L,
                 is_reservoir_change_ ? "TOTAL" : "DELTA");
    } else {
        ESP_LOGW(TAG, "[FEED_FILLING] HAL not available - using fallback timeout of 10 min");
    }

    // ENTRY: Log and activate water valve (only once!)
    if (!state_entry_executed_) {
        state_entry_executed_ = true;
        ESP_LOGI(TAG, "[FEED_FILLING] Starting tank fill before nutrient dosing");

        // Safety check - verify tank is EMPTY before filling (all 3 sensors OFF)
        if (hal_->hasWaterLevelSensors()) {
            bool high_sensor = hal_->readWaterLevelHigh();
            bool low_sensor = hal_->readWaterLevelLow();
            bool empty_sensor = hal_->readWaterLevelEmpty();

            // Tank must be empty: All sensors OFF
            bool tank_is_empty = (!high_sensor && !low_sensor && !empty_sensor);

            if (!tank_is_empty) {
                ESP_LOGE(TAG, "[FEED_FILLING] SAFETY ABORT: Tank not empty before fill");
                ESP_LOGE(TAG, "[FEED_FILLING] Sensors: HIGH=%s, LOW=%s, EMPTY=%s",
                         high_sensor ? "ON" : "OFF",
                         low_sensor ? "ON" : "OFF",
                         empty_sensor ? "ON" : "OFF");
                ESP_LOGE(TAG, "[FEED_FILLING] Tank must be completely drained before feeding");

                // Clear flags
                auto_ph_correction_pending_ = false;

                // Clear PSM event
                if (psm_) {
                    psm_->clearEvent();
                }

                // Abort sequence
                transitionTo(ControllerState::IDLE);
                return;
            }

            ESP_LOGI(TAG, "[FEED_FILLING] Safety check passed - tank empty (all sensors OFF)");
        }

        // Request water valve open with calculated max duration
        if (safety_gate_) {
            bool approved = safety_gate_->executeCommand(WATER_VALVE, true, max_fill_duration_sec);

            if (!approved) {
                ESP_LOGE(TAG, "[FEED_FILLING] SafetyGate rejected WaterValve command");

                // Clear flags
                auto_ph_correction_pending_ = false;

                // Clear PSM event
                if (psm_) {
                    psm_->clearEvent();
                }

                transitionTo(ControllerState::IDLE);
                return;
            }
        }

        ESP_LOGI(TAG, "[FEED_FILLING] Water valve OPENED (max duration: %d sec)", max_fill_duration_sec);
        return;
    }

    // MONITOR: Check water level sensors (if available)
    if (hal_->hasWaterLevelSensors()) {
        bool high_sensor = hal_->readWaterLevelHigh();
        bool low_sensor = hal_->readWaterLevelLow();

        // SUCCESS: Tank full when BOTH sensors are ON
        if (high_sensor && low_sensor) {
            ESP_LOGI(TAG, "[FEED_FILLING] BOTH sensors ON - tank full");
            ESP_LOGI(TAG, "[FEED_FILLING] Water level: HIGH=ON, LOW=ON");

            // Close water valve
            if (safety_gate_) {
                safety_gate_->executeCommand(WATER_VALVE, false, 0);
            }

            ESP_LOGI(TAG, "[FEED_FILLING] Fill complete - proceeding to nutrient dosing");

            // Transition to FEEDING (nutrients)
            // Note: auto_ph_correction_pending_ flag is already set
            transitionTo(ControllerState::FEEDING);
            return;
        }

        // LOG: Progress update every 5 seconds
        static uint32_t last_progress_log = 0;
        if (elapsed - last_progress_log >= 5000) {
            ESP_LOGI(TAG, "[FEED_FILLING] Filling in progress... HIGH=%s, LOW=%s",
                     high_sensor ? "ON" : "OFF", low_sensor ? "ON" : "OFF");
            last_progress_log = elapsed;
        }
    }

    // TIMEOUT: Safety backup - calculated max fill time
    if (elapsed >= max_fill_duration_ms) {
        ESP_LOGW(TAG, "[FEED_FILLING] Timeout reached (%d sec) - closing valve", max_fill_duration_sec);

        // Close water valve
        if (safety_gate_) {
            safety_gate_->executeCommand(WATER_VALVE, false, 0);
        }

        // Alert if sensors were expected but didn't trigger
        if (hal_->hasWaterLevelSensors()) {
            ESP_LOGE(TAG, "[FEED_FILLING] BOTH sensors never triggered - possible sensor failure or low water pressure");
        }

        ESP_LOGI(TAG, "[FEED_FILLING] Proceeding to nutrient dosing despite timeout");
        transitionTo(ControllerState::FEEDING);
        return;
    }
}

void PlantOSController::handleWaterFilling() {
    // Blue solid LED handled by LedBehaviorSystem
    uint32_t elapsed = getStateElapsed();

    // FILL DURATION: 10 minutes (fallback if sensors unavailable - CONFIGURE ACCORDING TO: tank volume and Mag Valve flowspeed )
    static constexpr uint32_t FILL_DURATION_MS = 600000;

    // ENTRY: Log and activate water valve (only once per state entry)
    static bool valve_command_sent = false;

    if (elapsed < 100 && !valve_command_sent) {
        ESP_LOGI(TAG, "[WATER_FILLING] Starting tank fill sequence");

        // Request water valve open (30s max duration as safety backup)
        if (safety_gate_) {
            bool approved = safety_gate_->executeCommand(WATER_VALVE, true, 30);  // 30 seconds max

            if (!approved) {
                ESP_LOGE(TAG, "[WATER_FILLING] SafetyGate rejected WaterValve command");

                // Set comprehensive alert
                if (ENHANCED_ERROR_HANDLING_ENABLED) {
                    status_logger_.updateAlertWithContext(
                        "PUMP_REJECTION_WATER_VALVE",
                        "Water valve rejected by SafetyGate",
                        "SafetyGate rejected water valve command",
                        "Check water valve wiring, verify valve not stuck, check SafetyGate max duration config",
                        "Water filling operation aborted",
                        "Manual intervention required. Check valve hardware.",
                        0
                    );
                }

                // Clear PSM event - fill aborted
                if (psm_) {
                    psm_->clearEvent();
                }

                valve_command_sent = false;  // Reset for next attempt
                transitionTo(ControllerState::IDLE);
                return;
            }
        }

        ESP_LOGI(TAG, "[WATER_FILLING] Water valve OPENED");
        valve_command_sent = true;  // Mark as sent
        return;
    }

    // MONITOR: Check water level sensors (if available)
    if (hal_->hasWaterLevelSensors()) {
        bool high_sensor = hal_->readWaterLevelHigh();
        bool low_sensor = hal_->readWaterLevelLow();

        // SUCCESS: High level reached
        if (high_sensor) {
            ESP_LOGI(TAG, "[WATER_FILLING] HIGH sensor triggered - tank full");

            // Close water valve
            if (safety_gate_) {
                safety_gate_->executeCommand(WATER_VALVE, false, 0);
            }

            // Resolve any active water-related alerts
            if (ENHANCED_ERROR_HANDLING_ENABLED) {
                status_logger_.resolveAlert("PUMP_REJECTION_WATER_VALVE");
                status_logger_.resolveAlert("HARDWARE_WATER_SENSOR_HIGH");
            }

            // Clear PSM event
            if (psm_) {
                psm_->clearEvent();
            }

            valve_command_sent = false;  // Reset for next fill cycle
            ESP_LOGI(TAG, "[WATER_FILLING] Fill complete");

            // Check if auto pH correction should follow
            if (auto_ph_correction_pending_) {
                ESP_LOGI(TAG, "Auto pH correction pending - starting pH check");
                auto_ph_correction_pending_ = false;

                // Reset pH correction state
                ph_attempt_count_ = 0;
                ph_readings_.clear();
                ph_current_ = 0.0f;
                ph_dose_ml_ = 0.0f;
                ph_dose_duration_ms_ = 0;

                // Log event for crash recovery
                if (psm_) {
                    psm_->logEvent("PH_CORRECTION", 0);  // 0 = STARTED
                }

                transitionTo(ControllerState::PH_PROCESSING);
            } else {
                ESP_LOGI(TAG, "Returning to IDLE");
                transitionTo(ControllerState::IDLE);
            }
            return;
        }

        // MONITOR: Log progress when LOW sensor triggered
        static bool low_sensor_logged = false;
        if (low_sensor && !low_sensor_logged) {
            ESP_LOGI(TAG, "[WATER_FILLING] LOW sensor triggered - filling in progress");
            low_sensor_logged = true;
        }
        if (!low_sensor) {
            low_sensor_logged = false;  // Reset for next fill cycle
        }
    } else {
        // FALLBACK: No sensors available - use time-based limit
        static bool no_sensor_warning_logged = false;
        if (!no_sensor_warning_logged) {
            ESP_LOGW(TAG, "[WATER_FILLING] Water level sensors not available - using 30s timeout");

            // Alert about missing sensors (informational)
            if (ENHANCED_ERROR_HANDLING_ENABLED) {
                ESP_LOGD(TAG, "Water level sensors not configured - operation will use timeout fallback");
            }

            no_sensor_warning_logged = true;
        }
    }

    // TIMEOUT: Safety backup - 30s max fill time
    if (elapsed >= FILL_DURATION_MS) {
        ESP_LOGW(TAG, "[WATER_FILLING] 30s timeout reached - closing valve");

        // Close water valve
        if (safety_gate_) {
            safety_gate_->executeCommand(WATER_VALVE, false, 0);
        }

        // Clear PSM event
        if (psm_) {
            psm_->clearEvent();
        }

        // Alert if sensors were expected but didn't trigger
        if (hal_->hasWaterLevelSensors()) {
            ESP_LOGE(TAG, "[WATER_FILLING] HIGH sensor never triggered - possible sensor failure or low water pressure");

            // Set comprehensive alert for sensor failure
            if (ENHANCED_ERROR_HANDLING_ENABLED) {
                status_logger_.updateAlertWithContext(
                    "HARDWARE_WATER_SENSOR_HIGH",
                    "HIGH water level sensor never triggered during fill",
                    "Sensor did not trigger after 30s timeout - possible sensor failure or disconnection",
                    "Check HIGH water sensor wiring (GPIO10), verify sensor power, test sensor with multimeter",
                    "Water filling completed via timeout (30s) instead of sensor trigger",
                    "Tank may be under-filled or sensor may be faulty. Manual verification recommended.",
                    0
                );
            }
        }

        valve_command_sent = false;  // Reset for next fill cycle
        ESP_LOGI(TAG, "[WATER_FILLING] Timeout complete");

        // Check if auto pH correction should follow
        if (auto_ph_correction_pending_) {
            ESP_LOGI(TAG, "Auto pH correction pending - starting pH check");
            auto_ph_correction_pending_ = false;

            // Reset pH correction state
            ph_attempt_count_ = 0;
            ph_readings_.clear();
            ph_current_ = 0.0f;
            ph_dose_ml_ = 0.0f;
            ph_dose_duration_ms_ = 0;

            // Log event for crash recovery
            if (psm_) {
                psm_->logEvent("PH_CORRECTION", 0);  // 0 = STARTED
            }

            transitionTo(ControllerState::PH_PROCESSING);
        } else {
            ESP_LOGI(TAG, "Returning to IDLE");
            transitionTo(ControllerState::IDLE);
        }
        return;
    }
}

void PlantOSController::handleWaterEmptying() {
    // Purple pulse LED handled by LedBehaviorSystem
    uint32_t elapsed = getStateElapsed();

    // Empty duration: 300 seconds (fallback if sensors unavailable)
    static constexpr uint32_t EMPTY_DURATION_MS = 300000;

    // ENTRY: Log and activate wastewater pump (only once per state entry)
    static bool pump_command_sent = false;

    if (elapsed < 100 && !pump_command_sent) {
        ESP_LOGI(TAG, "[WATER_EMPTYING] Starting tank drain sequence");

        // Check EMPTY sensor before starting (prevent dry-pump immediately)
        if (hal_->hasWaterLevelSensors() && !hal_->readWaterLevelEmpty()) {
            ESP_LOGW(TAG, "[WATER_EMPTYING] EMPTY sensor already OFF - tank already empty");

            // Clear PSM event
            if (psm_) {
                psm_->clearEvent();
            }

            pump_command_sent = false;  // Reset for next attempt

            // Check if part of reservoir change sequence
            if (auto_ph_correction_pending_) {
                ESP_LOGI(TAG, "[WATER_EMPTYING] Tank already empty - proceeding to fill phase");
                transitionTo(ControllerState::FEED_FILLING);
            } else {
                transitionTo(ControllerState::IDLE);
            }
            return;
        }

        // Request wastewater pump on (300s max duration as safety backup)
        if (safety_gate_) {
            bool approved = safety_gate_->executeCommand(WASTEWATER_PUMP, true, 300);  // 300 seconds max

            if (!approved) {
                ESP_LOGE(TAG, "[WATER_EMPTYING] SafetyGate rejected WastewaterPump command");

                // Set comprehensive alert
                if (ENHANCED_ERROR_HANDLING_ENABLED) {
                    status_logger_.updateAlertWithContext(
                        "PUMP_REJECTION_WASTEWATER",
                        "Wastewater pump rejected by SafetyGate",
                        "SafetyGate rejected wastewater pump command",
                        "Check wastewater pump wiring, verify pump not stuck, check SafetyGate max duration config",
                        "Water emptying operation aborted",
                        "Manual intervention required. Check pump hardware.",
                        0
                    );
                }

                // Clear PSM event - drain aborted
                if (psm_) {
                    psm_->clearEvent();
                }

                pump_command_sent = false;  // Reset for next attempt

                // Check if part of reservoir change sequence
                if (auto_ph_correction_pending_) {
                    ESP_LOGW(TAG, "[WATER_EMPTYING] Drain rejected - aborting reservoir change");
                    auto_ph_correction_pending_ = false;  // Clear flag - sequence aborted
                }
                transitionTo(ControllerState::IDLE);
                return;
            }
        }

        ESP_LOGI(TAG, "[WATER_EMPTYING] Wastewater pump ACTIVATED");
        pump_command_sent = true;  // Mark as sent
        return;
    }

    // MONITOR: Check water level sensors (if available)
    if (hal_->hasWaterLevelSensors()) {
        bool empty_sensor = hal_->readWaterLevelEmpty();
        bool low_sensor = hal_->readWaterLevelLow();

        // SUCCESS: EMPTY sensor OFF (tank below minimum safe level)
        if (!empty_sensor) {
            ESP_LOGI(TAG, "[WATER_EMPTYING] EMPTY sensor OFF - minimum safe level reached");

            // Stop wastewater pump
            if (safety_gate_) {
                safety_gate_->executeCommand(WASTEWATER_PUMP, false, 0);
            }

            // Resolve any active water-related alerts
            if (ENHANCED_ERROR_HANDLING_ENABLED) {
                status_logger_.resolveAlert("PUMP_REJECTION_WASTEWATER");
                status_logger_.resolveAlert("HARDWARE_WATER_SENSOR_LOW");
            }

            // Clear PSM event
            if (psm_) {
                psm_->clearEvent();
            }

            pump_command_sent = false;  // Reset for next drain cycle

            // Check if part of reservoir change sequence
            if (auto_ph_correction_pending_) {
                ESP_LOGI(TAG, "[WATER_EMPTYING] Drain complete - proceeding to fill phase");
                transitionTo(ControllerState::FEED_FILLING);
            } else {
                ESP_LOGI(TAG, "[WATER_EMPTYING] Drain complete - returning to IDLE");
                transitionTo(ControllerState::IDLE);
            }
            return;
        }

        // MONITOR: Log progress every 5 seconds
        static uint32_t last_progress_log = 0;
        if (elapsed - last_progress_log >= 5000) {
            ESP_LOGI(TAG, "[WATER_EMPTYING] Draining... LOW=%s, EMPTY=%s",
                     low_sensor ? "ON" : "OFF",
                     empty_sensor ? "ON" : "OFF");
            last_progress_log = elapsed;
        }
    } else {
        // FALLBACK: No sensors available - use time-based limit
        static bool no_sensor_warning_logged = false;
        if (!no_sensor_warning_logged) {
            ESP_LOGW(TAG, "[WATER_EMPTYING] Water level sensors not available - using 30s timeout");
            no_sensor_warning_logged = true;
        }
    }

    // TIMEOUT: Safety backup - 300s max drain time
    if (elapsed >= EMPTY_DURATION_MS) {
        ESP_LOGW(TAG, "[WATER_EMPTYING] 300s timeout reached - stopping pump");

        // Stop wastewater pump
        if (safety_gate_) {
            safety_gate_->executeCommand(WASTEWATER_PUMP, false, 0);
        }

        // Clear PSM event
        if (psm_) {
            psm_->clearEvent();
        }

        // Alert if sensors were expected but didn't trigger
        if (hal_->hasWaterLevelSensors()) {
            ESP_LOGE(TAG, "[WATER_EMPTYING] LOW sensor never cleared - possible sensor failure or clog");

            // Set comprehensive alert for sensor failure
            if (ENHANCED_ERROR_HANDLING_ENABLED) {
                status_logger_.updateAlertWithContext(
                    "HARDWARE_WATER_SENSOR_LOW",
                    "LOW water level sensor never cleared during drain",
                    "Sensor did not clear after 30s timeout - possible sensor failure, disconnection, or clog",
                    "Check LOW water sensor wiring (GPIO11), verify sensor power, test sensor, check for clogs",
                    "Water emptying completed via timeout (30s) instead of sensor trigger",
                    "Tank may not be fully drained or sensor may be faulty. Manual verification recommended.",
                    0
                );
            }
        }

        pump_command_sent = false;  // Reset for next drain cycle

        // Check if part of reservoir change sequence
        if (auto_ph_correction_pending_) {
            ESP_LOGI(TAG, "[WATER_EMPTYING] Timeout complete - proceeding to fill phase");
            transitionTo(ControllerState::FEED_FILLING);
        } else {
            ESP_LOGI(TAG, "[WATER_EMPTYING] Timeout complete - returning to IDLE");
            transitionTo(ControllerState::IDLE);
        }
        return;
    }
}

// ============================================================================
// pH Processing State Handler
// ============================================================================

void PlantOSController::handlePhProcessing() {
    // Yellow pulse - Processing periodic pH reading to decide if correction needed
    // Called every 2 hours (configurable in HAL) to check if pH is in range

    if (!hal_) {
        ESP_LOGE(TAG, "HAL not configured - cannot process pH");

        // Set comprehensive alert before ERROR transition
        status_logger_.updateAlertWithContext(
            "HARDWARE_HAL_MISSING",
            "HAL not configured",
            "Hardware Abstraction Layer pointer is null - dependency injection failed",
            "Critical system error - check plantOS.yaml configuration and ESPHome logs",
            "System initialization incomplete",
            "System cannot operate without HAL. Restart required.",
            0
        );

        transitionTo(ControllerState::ERROR);
        return;
    }

    // Read current pH value
    float ph_value = hal_->readPH();

    if (!hal_->hasPhValue()) {
        ESP_LOGW(TAG, "No pH value available - returning to IDLE");
        transitionTo(ControllerState::IDLE);
        return;
    }

    // Get pH range from HAL configuration
    float ph_min = hal_->get_ph_min();
    float ph_max = hal_->get_ph_max();

    ESP_LOGI(TAG, "========================================================");
    ESP_LOGI(TAG, "  PERIODIC pH CHECK");
    ESP_LOGI(TAG, "========================================================");
    ESP_LOGI(TAG, "Current pH: %.2f", ph_value);
    ESP_LOGI(TAG, "Target range: %.2f - %.2f", ph_min, ph_max);

    // Check if pH is within range
    if (ph_value >= ph_min && ph_value <= ph_max) {
        ESP_LOGI(TAG, "pH is within range - no correction needed");
        ESP_LOGI(TAG, "Returning to IDLE");
        ESP_LOGI(TAG, "========================================================");
        transitionTo(ControllerState::IDLE);
    } else if (ph_value > ph_max) {
        ESP_LOGW(TAG, "pH is TOO HIGH (%.2f > %.2f)", ph_value, ph_max);
        ESP_LOGI(TAG, "Starting pH correction sequence");
        ESP_LOGI(TAG, "========================================================");

        // Reset pH correction state
        ph_attempt_count_ = 0;
        ph_readings_.clear();
        ph_current_ = 0.0f;
        ph_dose_ml_ = 0.0f;
        ph_dose_duration_ms_ = 0;

        // Log event for crash recovery
        if (psm_) {
            psm_->logEvent("PH_CORRECTION", 0);  // 0 = STARTED
        }

        transitionTo(ControllerState::PH_MEASURING);
    } else {
        ESP_LOGW(TAG, "pH is TOO LOW (%.2f < %.2f)", ph_value, ph_min);
        ESP_LOGW(TAG, "WARNING: pH correction only supports LOWERING pH (adding acid)");
        ESP_LOGW(TAG, "Manual intervention required to raise pH");
        ESP_LOGI(TAG, "========================================================");
        transitionTo(ControllerState::IDLE);
    }
}

// ============================================================================
// Calibration Helper Methods
// ============================================================================

void PlantOSController::resetCalibrationBatch() {
    // Reset all calibration batch tracking variables
    ESP_LOGD(TAG, "Resetting calibration batch tracking");

    for (size_t i = 0; i < CALIB_TOTAL_BATCHES; i++) {
        calib_batch_averages_[i] = 0.0f;
    }

    calib_current_batch_ = 0;
    calib_readings_in_batch_ = 0;
    calib_batch_sum_ = 0.0f;
    calib_last_reading_time_ = 0;
    calib_batch_complete_time_ = 0;
}

bool PlantOSController::checkCalibrationStability() {
    // Check if last 3 batches are within 0.1 pH difference
    // Requires at least 3 batches to check

    if (calib_current_batch_ < 3) {
        ESP_LOGW(TAG, "Not enough batches to check stability (need 3, have %d)",
                 calib_current_batch_);
        return false;
    }

    // Get last 3 batch averages
    float batch1 = calib_batch_averages_[calib_current_batch_ - 3];
    float batch2 = calib_batch_averages_[calib_current_batch_ - 2];
    float batch3 = calib_batch_averages_[calib_current_batch_ - 1];

    // Calculate max difference between any two batches
    float diff_1_2 = std::abs(batch1 - batch2);
    float diff_2_3 = std::abs(batch2 - batch3);
    float diff_1_3 = std::abs(batch1 - batch3);

    float max_diff = std::max({diff_1_2, diff_2_3, diff_1_3});

    ESP_LOGI(TAG, "Stability check:");
    ESP_LOGI(TAG, "  Batch %d: pH %.2f", calib_current_batch_ - 2, batch1);
    ESP_LOGI(TAG, "  Batch %d: pH %.2f", calib_current_batch_ - 1, batch2);
    ESP_LOGI(TAG, "  Batch %d: pH %.2f", calib_current_batch_, batch3);
    ESP_LOGI(TAG, "  Max difference: %.3f pH (threshold: %.1f)",
             max_diff, CALIB_STABILITY_THRESHOLD);

    bool is_stable = max_diff <= CALIB_STABILITY_THRESHOLD;

    if (is_stable) {
        ESP_LOGI(TAG, "  Result: STABLE (difference within threshold)");
    } else {
        ESP_LOGW(TAG, "  Result: NOT STABLE (difference exceeds threshold)");
    }

    return is_stable;
}

void PlantOSController::sendTemperatureCompensation() {
    if (!hal_->hasTemperature()) {
        ESP_LOGW(TAG, "Temperature sensor not available - skipping compensation");
        return;
    }

    float temp = hal_->readTemperature();
    ESP_LOGI(TAG, "Sending temperature compensation: %.1f°C", temp);

    if (hal_->sendPhTemperatureCompensation(temp)) {
        ESP_LOGD(TAG, "Temperature compensation successful");
    } else {
        ESP_LOGW(TAG, "Temperature compensation failed");
    }
}

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

    if (!ph_sensor_) {
        ESP_LOGE(TAG, "Cannot start pH calibration - pH sensor component not configured");
        return;
    }

    // Check if sensor hardware is actually connected and responding
    if (!ph_sensor_->is_sensor_ready()) {
        ESP_LOGE(TAG, "Cannot start pH calibration - sensor hardware not responding");
        ESP_LOGE(TAG, "Check UART connection (TX=GPIO4, RX=GPIO5) and power to sensor");
        ESP_LOGE(TAG, "Verify sensor shows up in Central Status Logger");
        return;
    }

    ESP_LOGI(TAG, "===============================================");
    ESP_LOGI(TAG, "  STARTING 3-POINT pH CALIBRATION SEQUENCE");
    ESP_LOGI(TAG, "===============================================");
    ESP_LOGI(TAG, "Calibration points: 4.00 (low), 7.00 (mid), 10.01 (high)");
    ESP_LOGI(TAG, "Follow the prompts to insert probe into each solution");

    // Enable verbose mode for detailed UART logging during calibration
    if (ph_sensor_) {
        ph_sensor_->set_verbose(true);
        ESP_LOGI(TAG, "Verbose mode ENABLED for calibration sequence");
    }

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

void PlantOSController::startFillTank() {
    if (current_state_ == ControllerState::IDLE) {
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
        "FEEDING", "WATER_FILLING", "WATER_EMPTYING", "FEED_FILLING"
    };

    int state_index = static_cast<int>(current_state_);
    if (state_index >= 0 && state_index < 16) {  // Updated from 15 to 16
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
        "FEEDING", "WATER_FILLING", "WATER_EMPTYING", "FEED_FILLING"
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

uint32_t PlantOSController::calculatePhMixingDuration() const {
    // Linear formula based on tank volume:
    // - 1.7L → 30 seconds (30,000 ms)
    // - 70L → 120 seconds (120,000 ms)
    //
    // Formula: duration_ms = slope × volume_L + intercept
    // Slope = (120000 - 30000) / (70 - 1.7) = 90000 / 68.3 ≈ 1317.35 ms/L
    // Intercept = 30000 - (1317.35 × 1.7) ≈ 27760.5
    //
    // Final: duration_ms = 1317.35 × volume_L + 27760.5

    float volume_L = 10.0f;  // Default fallback
    if (hal_) {
        volume_L = hal_->getTankVolume();
    }

    // Apply linear formula
    constexpr float SLOPE = 1317.35f;       // ms per liter
    constexpr float INTERCEPT = 27760.5f;   // ms offset
    float duration_ms = SLOPE * volume_L + INTERCEPT;

    // Safety bounds: min 30s, max 120s
    if (duration_ms < 30000.0f) {
        ESP_LOGW(TAG, "Calculated mixing duration too short (%.1f s), using minimum 30s", duration_ms / 1000.0f);
        duration_ms = 30000.0f;
    } else if (duration_ms > 120000.0f) {
        ESP_LOGW(TAG, "Calculated mixing duration too long (%.1f s), using maximum 120s", duration_ms / 1000.0f);
        duration_ms = 120000.0f;
    }

    uint32_t duration_ms_int = static_cast<uint32_t>(duration_ms);
    ESP_LOGI(TAG, "pH mixing duration: %.1f seconds (tank: %.1f L)", duration_ms / 1000.0f, volume_L);

    return duration_ms_int;
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
    if (hal_) {
        hal_->stopAirPumpSequence(false);  // Stop sequence and turn OFF
        ESP_LOGI(TAG, "AirPump Shelly sequence stopped - pump OFF");
    } else {
        // Fallback to simple command if HAL not available
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

float PlantOSController::calculateAcidDoseML(float current_ph, float target_ph_max) {
    // Calculate acid dose in milliliters based on pH offset
    // Uses 0.5mL increments for precise control
    // Formula: dose_ml = pH_offset * calibration_factor (per liter of tank volume)

    if (current_ph <= target_ph_max) {
        return 0.0f; // No correction needed
    }

    float ph_offset = current_ph - target_ph_max;

    // Get tank volume from HAL
    float tank_volume_liters = hal_ ? hal_->getTankVolume() : 10.0f;

    // Calibration factor: 0.5mL per 0.1 pH unit per 10 liters of water
    // Example: pH 7.0 → 6.5 = 0.5 offset = 2.5mL for 10L tank
    // This is a conservative starting point - adjust based on your acid concentration
    float dose_ml_per_liter = ph_offset * 0.5f;  // 0.5mL per 0.1 pH per 10L
    float dose_ml = dose_ml_per_liter * (tank_volume_liters / 10.0f);

    // Round to nearest 0.5mL increment
    dose_ml = roundf(dose_ml * 2.0f) / 2.0f;

    // Ensure minimum dose of 0.5mL
    if (dose_ml < 0.5f) {
        dose_ml = 0.5f;
    }

    // Apply safety limit: max 5.0mL per dose
    if (dose_ml > 5.0f) {
        dose_ml = 5.0f;
    }

    ESP_LOGI(TAG, "Calculated acid dose: %.1f mL for %.2f pH offset (tank: %.1fL)",
             dose_ml, ph_offset, tank_volume_liters);

    return dose_ml;
}

float PlantOSController::calculateRobustPhAverage() {
    if (ph_readings_.empty()) {
        return 0.0f;
    }

    // Single reading - return as-is
    if (ph_readings_.size() == 1) {
        return ph_readings_[0];
    }

    // Sort readings for robust averaging
    std::vector<float> sorted = ph_readings_;
    std::sort(sorted.begin(), sorted.end());

    // Reject 10% from each end (outliers)
    size_t reject_count = sorted.size() / 10;
    if (reject_count == 0 && sorted.size() > 2) {
        reject_count = 1; // Reject at least 1 from each end
    }

    // Calculate average of middle values
    float sum = 0.0f;
    size_t count = 0;

    for (size_t i = reject_count; i < sorted.size() - reject_count; i++) {
        sum += sorted[i];
        count++;
    }

    if (count == 0) {
        // Fallback: use all readings
        for (float reading : sorted) {
            sum += reading;
        }
        count = sorted.size();
    }

    return sum / count;
}

bool PlantOSController::isPhStable() {
    // Need at least 3 readings to determine stability
    if (ph_readings_.size() < 3) {
        return false;
    }

    // Check if last 3 readings are within 0.1 pH
    constexpr float STABILITY_THRESHOLD = 0.1f;

    size_t last_idx = ph_readings_.size() - 1;
    float reading_1 = ph_readings_[last_idx];
    float reading_2 = ph_readings_[last_idx - 1];
    float reading_3 = ph_readings_[last_idx - 2];

    float max_val = std::max({reading_1, reading_2, reading_3});
    float min_val = std::min({reading_1, reading_2, reading_3});

    return (max_val - min_val) <= STABILITY_THRESHOLD;
}

bool PlantOSController::isNightModeHours() const {
    // Check if we're currently within night mode hours
    // Requires time source to be configured and synchronized

    if (!time_source_) {
        ESP_LOGW(TAG, "Time source not configured - cannot determine night mode hours");
        return false;
    }

    auto now = time_source_->now();
    if (!now.is_valid()) {
        ESP_LOGW(TAG, "NTP time not synchronized yet - cannot determine night mode hours");
        return false;
    }

    uint8_t current_hour = now.hour;

    // Handle wrapping case where start > end (e.g., 22:00 to 08:00)
    if (night_mode_start_hour_ > night_mode_end_hour_) {
        // Night mode spans midnight
        // In night mode if: hour >= start OR hour < end
        return (current_hour >= night_mode_start_hour_) || (current_hour < night_mode_end_hour_);
    } else {
        // Normal case: start < end (e.g., 01:00 to 06:00)
        // In night mode if: hour >= start AND hour < end
        return (current_hour >= night_mode_start_hour_) && (current_hour < night_mode_end_hour_);
    }
}

// ============================================================================
// Hardware Detection and Monitoring
// ============================================================================

void PlantOSController::checkHardwareStatus() {
    if (!ENHANCED_ERROR_HANDLING_ENABLED || !hal_) {
        return;
    }

    // Check temperature sensor availability
    if (!hal_->hasTemperature()) {
        // Temperature sensor not configured - this is informational only
        ESP_LOGD(TAG, "Temperature sensor not configured");
    } else {
        // Temperature sensor is configured - check if it's responding
        // We'll detect failures when trying to use it during operations
    }

    // Water level sensors are checked during WATER_FILLING and WATER_EMPTYING operations
    // We don't need to check them here since they're only used during those specific states
}

// ============================================================================
// Shelly Health Check
// ============================================================================

void PlantOSController::checkShellyHealth() {
    if (!hal_) {
        return;
    }

    uint32_t now = hal_->getSystemTime();

    // Only update status logger every 30 seconds
    if (now - last_shelly_check_time_ < SHELLY_CHECK_INTERVAL) {
        return;
    }
    last_shelly_check_time_ = now;

    // Read cached Shelly health status from HAL
    // The actual ping is performed by a YAML interval script that calls hal.updateShellyHealth()
    bool reachable = hal_->isShellyReachable();
    uint32_t uptime = hal_->getShellyUptime();

    ESP_LOGD(TAG, "Shelly health status: %s (uptime: %us)",
             reachable ? "ONLINE" : "OFFLINE", uptime);

    // Update CentralStatusLogger with result
    std::vector<ShellyDeviceInfo> devices;
    devices.push_back(ShellyDeviceInfo(
        "Shelly Plus 4PM",
        "192.168.0.130",
        reachable,
        true,  // Critical device
        reachable ? "Online" : "Offline"
    ));
    if (reachable) {
        devices[0].uptime_seconds = uptime;
        devices[0].last_seen_ms = now;
    }
    status_logger_.updateShellyHardwareStatus(devices);

    // Update alert status based on reachability
    if (!reachable) {
        status_logger_.updateAlertStatus("SHELLY_OFFLINE",
            "Shelly Plus 4PM not responding to HTTP ping");
    } else {
        status_logger_.clearAlert("SHELLY_OFFLINE");
    }
}

// ============================================================================
// Operation Retry Framework Helpers
// ============================================================================

void PlantOSController::initOperationRetry(const std::string& operation_name, uint8_t max_retries) {
    if (!ENHANCED_ERROR_HANDLING_ENABLED) {
        return;
    }

    retry_state_.reset(operation_name, max_retries);
    ESP_LOGI(TAG, "Operation retry initialized: %s (max retries: %u)",
             operation_name.c_str(), max_retries);
}

bool PlantOSController::canRetryOperation() {
    if (!ENHANCED_ERROR_HANDLING_ENABLED) {
        return false;
    }

    return retry_state_.canRetry();
}

void PlantOSController::recordOperationStep(const std::string& step_name) {
    if (!ENHANCED_ERROR_HANDLING_ENABLED) {
        return;
    }

    retry_state_.completed_steps.push_back(step_name);
    ESP_LOGD(TAG, "Operation step completed: %s", step_name.c_str());
}

void PlantOSController::retryOperation() {
    if (!ENHANCED_ERROR_HANDLING_ENABLED) {
        return;
    }

    retry_state_.incrementRetry();

    ESP_LOGW(TAG, "Retrying operation '%s': attempt %u/%u (backoff: %u ms)",
             retry_state_.operation_name.c_str(),
             retry_state_.retry_count + 1,
             retry_state_.max_retries,
             retry_state_.backoff_delay_ms);
}

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
    if (!hal_) {
        return 0;
    }

    // Get current Unix timestamp
    int64_t now = hal_->getCurrentTimestamp();

    if (now == 0) {
        ESP_LOGW(TAG, "[AUTO-FEEDING] NTP time unavailable");
        return 0;
    }

    // Calculate midnight UTC (start of current day)
    // Unix time / 86400 = days since epoch
    // Multiply back by 86400 = timestamp at midnight
    int64_t days_since_epoch = now / 86400;
    int64_t midnight_timestamp = days_since_epoch * 86400;

    return midnight_timestamp;
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
