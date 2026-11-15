#include "controller.h"
#include "schedule_math.h"
#include "esphome/components/plantos_hal/hal.h"
#include "esphome/components/actuator_safety_gate/ActuatorSafetyGate.h"
#include "esphome/components/persistent_state_manager/persistent_state_manager.h"
#include "esphome/components/calendar_manager/CalendarManager.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/alert_service/alert_service.h"
#include <algorithm> // for std::sort, std::min, std::max
#include <cmath>    // for std::sqrt
#include <cstring>  // for strcmp

// PlantOS controller core: setup(), loop(), FSM dispatch, transitionTo(), the public API and the
// INIT / IDLE / NIGHT / SHUTDOWN / PAUSE / ERROR handlers.
//
// FSMINFO.md (repo root) is the authoritative source for FSM behavior and MUST be updated when
// states, transitions, actuator actions or timeouts change.
//
// The rest of PlantOSController lives in:
//   ph_control.cpp     PH_* handlers, pH calibration, acid dose and K-factor
//   ec_control.cpp     FEEDING, EC_* handlers, EC calibration, EC K-factor
//   water_ops.cpp      WATER_FILLING, WATER_EMPTYING, FEED_FILLING
//   health_checks.cpp  hardware/Shelly/sensor checks, calibration reminders, operation retry
//   scheduling.cpp     night mode, grow day, auto-feed and auto-reservoir-change triggers
//   dosing_math.*, schedule_math.*  pure functions (no ESPHome includes, host-tested)

namespace plantos_controller {

PlantOSController::PlantOSController() {
    // Initialize LED behavior system
    led_behaviors_ = std::make_unique<LedBehaviorSystem>();
}

// ============================================================================
// ESPHome Component Lifecycle
// ============================================================================

void PlantOSController::setup() {
    // Verify critical dependencies
    if (!hal_) {
        ESP_LOGE(TAG, "HAL not configured - Controller cannot function!");
        return;
    }

    if (!safety_gate_) {
        ESP_LOGW(TAG, "SafetyGate not configured - Actuator control disabled");
    }

    // Active preset first: tank volumes, blocked actuators and calendar index must be in place
    // before INIT, PSM recovery and the first calendar date sync (all run from loop()).
    setupPresets();

    if (psm_) {
        // Load auto-feeding state from NVS
        auto_feeding_enabled_ = psm_->loadState(NVS_KEY_AUTO_FEED_ENABLE, true);

        // Load auto-fill state from NVS (default OFF - safer)
        auto_fill_enabled_ = psm_->loadState(NVS_KEY_AUTO_FILL_ENABLE, false);

        // Load auto pH correction state from NVS (default ON)
        auto_ph_correction_enabled_ = psm_->loadState(NVS_KEY_AUTO_PH_CORR, true);

        // Load adaptive pH K-factor from NVS
        ph_K_ = psm_->loadFloat(NVS_KEY_PH_K, 0.07f);

        // Load adaptive EC K-factor from NVS
        ec_K_feed_ = psm_->loadFloat(NVS_KEY_EC_K, 0.00015f);

        // Load calibration timestamps from NVS (Phase 7)
        last_ph_calibration_ts_ = (int64_t)psm_->loadInt32(NVS_KEY_PH_CALIB_TS, 0);
        last_ec_calibration_ts_ = (int64_t)psm_->loadInt32(NVS_KEY_EC_CALIB_TS, 0);
        ESP_LOGI(TAG, "NVS: auto feed=%d fill=%d pH=%d, pH K=%.4f, EC K=%.6f, calibrated pH=%lld EC=%lld",
                 auto_feeding_enabled_, auto_fill_enabled_, auto_ph_correction_enabled_, ph_K_, ec_K_feed_,
                 (long long)last_ph_calibration_ts_, (long long)last_ec_calibration_ts_);
    } else {
        ESP_LOGW(TAG, "No PSM - state persistence disabled");
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
    } else {
        ESP_LOGW(TAG, "No temperature sensor - pH temperature compensation off");
    }

    // Initialize periodic pH check timer
    last_ph_check_time_ = esphome::millis();
    if (hal_->hasTime()) {
        last_ph_check_timestamp_ = hal_->getCurrentTimestamp();
    } else {
        last_ph_check_timestamp_ = 0;
    }
    ESP_LOGI(TAG, "Periodic pH check every %u h (%s)", (unsigned)(hal_->get_ph_reading_interval() / 3600000),
             hal_->hasTime() ? "clock slots" : "no clock yet: uptime based");

    // Always start in INIT state for boot sequence
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
        ESP_LOGD(TAG, "Temperature changed: %.1f C", last_temperature_);
    }

    updateStatusReport();
    updateActuatorTests();
    resendRecoveryDrainOff();

    // Call state-specific handler
    switch (current_state_) {
        case ControllerState::INIT: handleInit(); break;
        case ControllerState::IDLE: handleIdle(); break;
        case ControllerState::NIGHT: handleNight(); break;
        case ControllerState::SHUTDOWN: handleShutdown(); break;
        case ControllerState::PAUSE: handlePause(); break;
        case ControllerState::ERROR: handleError(); break;
        case ControllerState::PH_PROCESSING: handlePhProcessing(); break;
        case ControllerState::PH_MEASURING: handlePhMeasuring(); break;
        case ControllerState::PH_CALCULATING: handlePhCalculating(); break;
        case ControllerState::PH_INJECTING: handlePhInjecting(); break;
        case ControllerState::PH_MIXING: handlePhMixing(); break;
        case ControllerState::PH_CALIBRATING: handlePhCalibrating(); break;
        case ControllerState::FEEDING: handleFeeding(); break;
        case ControllerState::WATER_FILLING: handleWaterFilling(); break;
        case ControllerState::WATER_EMPTYING: handleWaterEmptying(); break;
        case ControllerState::FEED_FILLING: handleFeedFilling(); break;
        case ControllerState::EC_PROCESSING: handleEcProcessing(); break;
        case ControllerState::EC_CALCULATING: handleEcCalculating(); break;
        case ControllerState::EC_FEEDING: handleEcFeeding(); break;
        case ControllerState::EC_MIXING: handleEcMixing(); break;
        case ControllerState::EC_MEASURING: handleEcMeasuring(); break;
        case ControllerState::EC_CALIBRATING: handleEcCalibrating(); break;
    }

    controlGrowLight();
}

void PlantOSController::controlGrowLight() {
    // ========================================================================
    // Time-based Light Control
    // ========================================================================
    // Check if we should update grow light based on calendar schedule
    // Only run when:
    // - Grow light schedule is enabled (default: disabled, use Shelly scripts instead)
    // - System is operational (not in SHUTDOWN, PAUSE, or ERROR)
    // - The active preset has the grow light (FN_GROW_LIGHT)
    if (grow_light_schedule_enabled_ && profile_.has(preset_model::FN_GROW_LIGHT) && calendar_manager_ && time_source_ &&
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
            // Handles the wraparound case (e.g., ON at 16:00, OFF at 08:00 means light is on overnight)
            bool should_be_on = schedule_math::lightShouldBeOn(current_minutes, schedule.light_on_time,
                                                               schedule.light_off_time);

            // Update light state if changed (or on first run)
            // Uses requestPump to go through ASG for proper tracking
            if (grow_light_first_run_ || grow_light_on_ != should_be_on) {
                ESP_LOGI(TAG, "Grow light schedule: %s (day %d, %02d:%02d-%02d:%02d)", should_be_on ? "ON" : "OFF",
                         schedule.day_number, schedule.light_on_time / 60, schedule.light_on_time % 60,
                         schedule.light_off_time / 60, schedule.light_off_time % 60);
                if (should_be_on) {
                    requestPump("GrowLight", true, 0);  // 0 = no duration limit
                } else {
                    requestPump("GrowLight", false);
                }
                grow_light_on_ = should_be_on;
                grow_light_first_run_ = false;
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

    // ========================================================================
    // ONE-TIME BOOT ACTIONS (Phase 8 - Enhanced Reboot Recovery)
    // Runs once during INIT, regardless of elapsed time.
    // Cooldowns, sensor check, reboot alert.
    // ========================================================================
    if (!boot_alert_sent_) {
        boot_alert_sent_ = true;
        uint32_t now = hal_->getSystemTime();

        last_sensor_health_check_time_ = now;

        // Boot sensor check: one line, "-" = not available
        char ph_s[8] = "-", ec_s[10] = "-", t_s[8] = "-", lvl_s[8] = "-";
        if (hal_->hasPhValue()) snprintf(ph_s, sizeof(ph_s), "%.2f", hal_->readPH());
        if (hal_->hasECValue()) snprintf(ec_s, sizeof(ec_s), "%.0f", hal_->readEC());
        if (hal_->hasTemperature()) snprintf(t_s, sizeof(t_s), "%.1f", hal_->readTemperature());
        if (hal_->hasWaterLevelSensors()) {
            snprintf(lvl_s, sizeof(lvl_s), "%d%d%d", hal_->readWaterLevelHigh() ? 1 : 0,
                     hal_->readWaterLevelLow() ? 1 : 0, hal_->readWaterLevelEmpty() ? 1 : 0);
        }
        ESP_LOGI(TAG, "Boot sensors: pH %s, EC %s uS/cm, water %s C, level H/L/E %s", ph_s, ec_s, t_s, lvl_s);

        // Reboot notification via AlertService
        if (alert_service_) {
            alert_service_->alert(alert_service::AlertLevel::INFO,
                "System rebooted",
                "PlantOS controller boot complete — all subsystems initializing");
        }
    }

    if (elapsed >= INIT_DURATION) {
        // PSM recovery once, at the end of INIT: every component is set up by then. (Until
        // 2026-11-15 it ran only while elapsed < 100 ms, a window the first loop() after boot
        // usually misses; the IDLE hold check covered SHUTDOWN/PAUSE.)
        recoverFromPsm();
        if (boot_restore_pending_) {
            boot_restore_pending_ = false;

            if (boot_restore_state_ == ControllerState::SHUTDOWN) {
                if (alert_service_) {
                    alert_service_->alert(alert_service::AlertLevel::WARNING,
                        "State restored: SHUTDOWN",
                        "System was in SHUTDOWN before power loss — resuming SHUTDOWN");
                }
                transitionTo(ControllerState::SHUTDOWN);
            } else if (boot_restore_state_ == ControllerState::PAUSE) {
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

void PlantOSController::recoverFromPsm() {
    // Boot recovery from the PSM event (FSMINFO.md "Persistent State Manager (PSM) Events").
    // Only OFF commands are sent: they pass the preset block and the forced flag skips the gate's
    // "already off" debounce (after a reset the gate tracks every actuator as OFF).
    if (!psm_ || !psm_->hasEvent()) {
        ESP_LOGD(TAG, "No PSM event - clean boot");
        return;
    }
    esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
    const char *id = event.eventID;
    long long age = (long long) psm_->getEventAge();
    if (id[0] == '\0') {
        return;
    }
    auto is = [id](const char *name) { return strcmp(id, name) == 0; };

    if (is(psm_event::STATE_SHUTDOWN) || is(psm_event::STATE_PAUSE)) {
        boot_restore_state_ = is(psm_event::STATE_SHUTDOWN) ? ControllerState::SHUTDOWN : ControllerState::PAUSE;
        boot_restore_pending_ = true;
        ESP_LOGW(TAG, "Recovery: %s in PSM (age %lld s), restoring after INIT", id, age);
        return;
    }

    // Water operations: the valve is a GPIO output (off after a reset anyway), the drain pump a
    // Shelly socket that keeps running through an ESP reset - switch both OFF explicitly.
    bool stop_valve = is(psm_event::WATER_FILL) || is(psm_event::FEED_OPERATION) || is(psm_event::RESERVOIR_CHANGE);
    bool stop_drain = is(psm_event::WATER_EMPTY) || is(psm_event::RESERVOIR_CHANGE);
    std::string name(id);  // the event is cleared below
    if (stop_valve && safety_gate_) {
        safety_gate_->executeCommand(WATER_VALVE, false, 0, true);
    }
    if (stop_drain && safety_gate_) {
        safety_gate_->executeCommand(WASTEWATER_PUMP, false, 0, true);
        recovery_drain_off_pending_ = true;  // WiFi is not up yet: repeat once the Shelly answers
    }
    if (stop_valve || stop_drain) {
        psm_->clearEvent();
        ESP_LOGW(TAG, "Recovery: %s interrupted (age %lld s), %s%s OFF, PSM cleared", name.c_str(), age,
                 stop_valve ? "WaterValve" : "", stop_drain ? (stop_valve ? " + WastewaterPump" : "WastewaterPump") : "");
    } else {
        ESP_LOGW(TAG, "Recovery: %s interrupted (status %d, age %lld s), no automatic action", name.c_str(),
                 event.status, age);
    }
    if (alert_service_) {
        alert_service_->alert(alert_service::AlertLevel::WARNING, "Operation interrupted",
                              "Operation '" + name + "' was interrupted by power loss (age: " + std::to_string(age) +
                                  "s) — verify system state");
    }
}

void PlantOSController::resendRecoveryDrainOff() {
    // The recovery's drain OFF at INIT goes out before WiFi is up and usually fails; repeat it once
    // the Shelly answers (a new drain started meanwhile wins).
    if (!recovery_drain_off_pending_ || !hal_ || !hal_->isShellyReachable()) {
        return;
    }
    recovery_drain_off_pending_ = false;
    if (current_state_ == ControllerState::WATER_EMPTYING || !safety_gate_) {
        return;
    }
    ESP_LOGI(TAG, "Recovery: WastewaterPump OFF repeated (Shelly reachable)");
    safety_gate_->executeCommand(WASTEWATER_PUMP, false, 0, true);
}

bool PlantOSController::checkPersistedHoldState(uint32_t &last_check_ms, uint32_t now) {
    // Every 5 s in IDLE / NIGHT: a STATE_SHUTDOWN or STATE_PAUSE event in the PSM wins. This covers
    // a state set while running, a restore in INIT that failed on timing, and component init order.
    if (now - last_check_ms < 5000) {
        return false;
    }
    last_check_ms = now;
    if (!psm_ || !psm_->hasEvent()) {
        return false;
    }
    esphome::persistent_state_manager::CriticalEventLog event = psm_->getLastEvent();
    ESP_LOGD(TAG, "%s PSM check: '%s' status %d age %lld s", stateName(current_state_), event.eventID,
             event.status, (long long)psm_->getEventAge());
    ControllerState hold;
    if (strcmp(event.eventID, psm_event::STATE_SHUTDOWN) == 0) {
        hold = ControllerState::SHUTDOWN;
    } else if (strcmp(event.eventID, psm_event::STATE_PAUSE) == 0) {
        hold = ControllerState::PAUSE;
    } else {
        return false;
    }
    ESP_LOGW(TAG, "PSM holds %s - leaving %s", event.eventID, stateName(current_state_));
    transitionTo(hold);
    return true;
}

void PlantOSController::syncCalendarDate() {
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
}

bool PlantOSController::checkPeriodicPh(uint32_t now) {
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

        // Check if we should trigger a pH check:
        // ≥24 h interval → every N days, 1 minute after midnight (e.g. 48h → every 2 days)
        // <24 h interval → slots from midnight (e.g. 2h → 0:00, 2:00, 4:00, ..., 22:00)
        const schedule_math::PhCheckSlot ph_slot = schedule_math::phCheckDue(
            current_timestamp, last_ph_check_timestamp_, hal_->getSecondsSinceMidnight(), ph_interval_seconds);
        bool should_trigger = ph_slot.due;

        if (should_trigger) {
            last_ph_check_timestamp_ = current_timestamp;
            last_ph_check_time_ = now;  // Update fallback timer too

            if (!auto_ph_correction_enabled_ || (night_mode_enabled_ && isNightModeHours())) {
                ESP_LOGD(TAG, "Automatic pH check skipped — %s",
                         !auto_ph_correction_enabled_ ? "auto pH correction disabled" : "night mode active");
            } else {
                if (ph_slot.multi_day) {
                    ESP_LOGI(TAG, "Auto pH check: %lld days since last (interval %lld days)",
                             (long long)ph_slot.days_since_last, (long long)ph_slot.interval_days);
                } else {
                    ESP_LOGI(TAG, "Auto pH check: slot %02u:%02u", (unsigned)ph_slot.slot_hour,
                             (unsigned)ph_slot.slot_minute);
                }

                transitionTo(ControllerState::PH_PROCESSING);
                return true;  // Exit IDLE immediately
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
                ESP_LOGW(TAG, "Auto pH check (no clock: every %u h since boot)", (unsigned)(ph_interval_ms / 3600000));

                transitionTo(ControllerState::PH_PROCESSING);
                return true;  // Exit IDLE immediately
            }
        }
    }
    return false;
}

bool PlantOSController::checkAutoTriggers() {
    // ========================================================================
    // AUTOMATIC FEEDING CHECK - Once per day when water level reaches LOW
    // ========================================================================
    if (shouldTriggerAutoFeeding()) {
        ESP_LOGI(TAG, "Auto-feed: level LOW (H/L/E=001), not fed today");

        // Record today's local date (own NVS key, so it survives the PSM event that startFeed() logs)
        markAutoFeedDay(getLocalDateOrdinal());

        // Start feeding sequence
        startFeed();
        return true;
    }

    // ========================================================================
    // AUTOMATIC WATER FILL CHECK - When EMPTY sensor triggers (Phase 6)
    // ========================================================================
    if (auto_fill_enabled_ && operationAvailable(Operation::FILL) && hal_ && hal_->hasWaterLevelSensors()) {
        // Trigger when tank is empty (HIGH=OFF, LOW=OFF, EMPTY=ON)
        // and not in night mode / not auto-feeding about to trigger
        bool high = hal_->readWaterLevelHigh();
        bool low = hal_->readWaterLevelLow();
        bool empty = hal_->readWaterLevelEmpty();
        bool tank_empty = (!high && !low && empty);

        if (tank_empty && !(night_mode_enabled_ && isNightModeHours())) {
            ESP_LOGI(TAG, "Auto-fill: level LOW (H/L/E=001)");
            startFillTank();
            return true;
        }
    }

    // ========================================================================
    // AUTOMATIC RESERVOIR CHANGE CHECK - Once per week on configured day
    // ========================================================================
    if (shouldTriggerAutoReservoirChange()) {
        ESP_LOGI(TAG, "Auto reservoir change: weekday %d", auto_reservoir_change_day_);

        // Record this week (own NVS key, so it survives the PSM event that startReservoirChange() logs)
        markAutoResWeek(getCurrentWeekNumber());

        // Start reservoir change sequence
        startReservoirChange();
        return true;
    }
    return false;
}

void PlantOSController::handleIdle() {
    // Breathing green - waiting for commands. Order matters: PSM hold, date sync, periodic pH,
    // post-fill EC check, night, Shelly health, auto-feed / auto-fill / auto reservoir change,
    // sensor health.
    uint32_t now = hal_->getSystemTime();
    if (checkPersistedHoldState(idle_psm_check_ms_, now)) {
        return;
    }

    syncCalendarDate();

    if (checkPeriodicPh(now)) {
        return;
    }

    // ========================================================================
    // POST-FILL EC CHECK - triggered by auto_ec_check_pending_ after WATER_FILLING
    // Requires: auto-feeding enabled, not night mode hours
    // ========================================================================
    if (auto_ec_check_pending_ && auto_feeding_enabled_ && !(night_mode_enabled_ && isNightModeHours())) {
        ESP_LOGI(TAG, "EC check after water fill");
        transitionTo(ControllerState::EC_PROCESSING);
        return;
    }

    // Check if night mode hours started - transition to NIGHT
    if (night_mode_enabled_ && isNightModeHours()) {
        transitionTo(ControllerState::NIGHT);
        return;
    }

    // ========================================================================
    // SHELLY HEALTH CHECK - Ping Shelly device periodically (every 30 seconds)
    // ========================================================================
    checkShellyHealth();

    if (checkAutoTriggers()) {
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
    if (checkPersistedHoldState(night_psm_check_ms_, hal_->getSystemTime())) {
        return;
    }

    // Check if night mode hours ended - transition back to IDLE
    if (!night_mode_enabled_ || !isNightModeHours()) {
        ESP_LOGI(TAG, "Night mode %s", night_mode_enabled_ ? "hours ended" : "disabled");
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

    // On entry: Turn off all pumps and valves for safety (only once!)
    if (!state_entry_executed_) {
        state_entry_executed_ = true;
        turnOffAllPumps();

        // Persist SHUTDOWN state to NVS
        if (psm_) {
            psm_->logEvent(psm_event::STATE_SHUTDOWN, 1);  // 1 = ENTERED_SHUTDOWN
            ESP_LOGI(TAG, "SHUTDOWN logged to PSM (%s)", psm_->hasEvent() ? "saved" : "NOT saved");
        } else {
            ESP_LOGE(TAG, "No PSM - SHUTDOWN not persisted");
        }

        return;
    }

    // Stay in this state until setToIdle() is called
    // No automatic exit - persists across reboots
}

void PlantOSController::handlePause() {
    // Solid orange LED handled by LedBehaviorSystem
    // Actuators maintain current state, calendar/time-based events disabled
    // Persists across power cycles - must use setToIdle() to exit

    // On entry: Log state change (only once!)
    if (!state_entry_executed_) {
        state_entry_executed_ = true;

        // Persist PAUSE state to NVS
        if (psm_) {
            psm_->logEvent(psm_event::STATE_PAUSE, 1);  // 1 = ENTERED_PAUSE
            ESP_LOGI(TAG, "PAUSE logged to PSM (%s)", psm_->hasEvent() ? "saved" : "NOT saved");
        } else {
            ESP_LOGE(TAG, "No PSM - PAUSE not persisted");
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
        ESP_LOGE(TAG, "ERROR: all pumps OFF");
        return;
    }

    // Wait 5 seconds to display error
    if (elapsed >= ERROR_DURATION) {
        {
            // Resolve all active alerts (keep history for debugging)
            ESP_LOGI(TAG, "Error timeout - resolving alerts, restart via INIT");

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
        }

        transitionTo(ControllerState::INIT);
    }
}

// ============================================================================
// Public API
// ============================================================================

bool PlantOSController::canStart(Operation op, const char **reason) const {
    // Every manual or automatic start needs IDLE (NIGHT blocks all operations).
    if (current_state_ == ControllerState::NIGHT) {
        *reason = "night mode";
        return false;
    }
    if (current_state_ != ControllerState::IDLE) {
        *reason = "busy";
        return false;
    }
    // Functions of the active preset (capability table: operationFunctions(), presets.cpp)
    const uint8_t missing = preset_model::missingFunctions(operationFunctions(op), profile_.current().mask);
    if (missing != 0) {
        *reason = missingFunctionsReason(missing);
        return false;
    }
    // Hardware each operation needs
    switch (op) {
        case Operation::FILL:
            if (!hal_ || !hal_->hasWaterLevelSensors()) {
                *reason = "no water level sensors (overflow risk)";
                return false;
            }
            break;
        case Operation::DRAIN:
        case Operation::RESERVOIR_CHANGE:
            if (!hal_->isShellyEnabled()) {
                *reason = "Shelly integration disabled (no wastewater pump)";
                return false;
            }
            break;
        case Operation::PH_CALIBRATION:
            if (!hal_->isPhSensorReady()) {
                *reason = "pH sensor not responding (UART GPIO18/19)";
                return false;
            }
            break;
        case Operation::EC_CALIBRATION:
            if (!hal_->hasECValue()) {
                *reason = "no EC reading";
                return false;
            }
            break;
        default:
            break;
    }
    return true;
}

bool PlantOSController::startAllowed(Operation op, const char *name) {
    const char *reason = "";
    if (canStart(op, &reason)) {
        return true;
    }
    ESP_LOGW(TAG, "Cannot start %s: %s (state %s)", name, reason, stateName(current_state_));
    recordRefusal(name, reason);
    return false;
}

void PlantOSController::resetPhCycle() {
    ph_attempt_count_ = 0;
    ph_readings_.clear();
    ph_current_ = 0.0f;
    ph_dose_ml_ = 0.0f;
    ph_dose_duration_ms_ = 0;
    ph_cycle_start_ph_ = 0.0f;
    ph_cycle_total_ml_ = 0.0f;
    ph_cycle_water_filled_ = false;
    ph_cycle_aborted_ = false;
}

void PlantOSController::startPhCorrection() {
    if (!startAllowed(Operation::PH_CORRECTION, "pH correction")) return;

    ESP_LOGI(TAG, "Manual pH correction");
    resetPhCycle();

    // Manual trigger always uses the normal 30s stabilization: clear any stale
    // post-feed/post-fill intent flags that may linger from an aborted auto-flow.
    ph_post_fill_stabilize_ = false;
    ph_post_feed_stabilize_ = false;

    if (psm_) {
        psm_->logEvent(psm_event::PH_CORRECTION, 0);  // 0 = STARTED
    }
    transitionTo(ControllerState::PH_MEASURING);
}

void PlantOSController::startPhCalibration() {
    if (!startAllowed(Operation::PH_CALIBRATION, "pH calibration")) return;

    ESP_LOGI(TAG, "Starting 3-point pH calibration: 7.00 -> 4.00 -> 10.01, follow the prompts");

    // Verbose UART logging of the pH sensor during calibration
    hal_->setPhVerbose(true);

    calib_step_ = CalibrationStep::MID_PROMPT;
    calib_step_start_time_ = esphome::millis();
    logPhCalibrationPrompt(0);

    if (psm_) {
        psm_->logEvent(psm_event::PH_CALIBRATION, 0);  // 0 = STARTED
    }
    transitionTo(ControllerState::PH_CALIBRATING);
}

void PlantOSController::startFeeding() {
    if (!startAllowed(Operation::FEEDING, "feeding")) return;

    ESP_LOGI(TAG, "Manual feeding");
    if (psm_) {
        psm_->logEvent(psm_event::FEEDING, 0);  // 0 = STARTED
    }
    transitionTo(ControllerState::FEEDING);
}

void PlantOSController::startMotherNutrients(float liters) {
    if (!startAllowed(Operation::MOTHER_MIX, "mother mix")) return;
    if (liters <= 0.0f || liters > 15.0f) {
        ESP_LOGW(TAG, "Mother mix: invalid volume %.1f L (1-15)", liters);
        return;
    }

    ec_dose_A_ml_ = MOTHER_MIX_A_ML_PER_L * liters;
    ec_dose_B_ml_ = MOTHER_MIX_B_ML_PER_L * liters;
    ec_dose_C_ml_ = MOTHER_MIX_C_ML_PER_L * liters;
    ec_total_ml_per_L_ = 0.0f;
    ec_attempt_count_ = 0;
    feeding_manual_mode_ = false;
    is_mother_mix_ = true;

    ESP_LOGI(TAG, "Mother mix %.1f L: A %.2f mL, B %.2f mL, C %.2f mL (A->B->C->IDLE, no EC loop)",
             liters, ec_dose_A_ml_, ec_dose_B_ml_, ec_dose_C_ml_);

    if (psm_) psm_->logEvent(psm_event::MOTHER_MIX, 0);
    transitionTo(ControllerState::EC_FEEDING);
}

void PlantOSController::startFillTank() {
    // SAFETY: canStart(FILL) refuses without water level sensors - the valve would run
    // blind on a timeout with no feedback, risking tank overflow
    if (!startAllowed(Operation::FILL, "fill")) return;

    ESP_LOGI(TAG, "Tank fill (then EC check and pH correction)");
    auto_ph_correction_pending_ = true;

    if (psm_) {
        psm_->logEvent(psm_event::WATER_FILL, 0);  // 0 = STARTED
    }
    transitionTo(ControllerState::WATER_FILLING);
}

void PlantOSController::startEmptyTank() {
    // The wastewater pump is a Shelly socket - no Shelly, no drain
    if (!startAllowed(Operation::DRAIN, "tank drain")) return;

    ESP_LOGI(TAG, "Tank drain until EMPTY sensor clears");
    if (psm_) {
        psm_->logEvent(psm_event::WATER_EMPTY, 0);  // 0 = STARTED
    }
    transitionTo(ControllerState::WATER_EMPTYING);
}

void PlantOSController::startFeed() {
    if (!startAllowed(Operation::FEED, "feed")) return;

    // SAFETY CHECK: Verify tank is empty (HIGH and LOW off) before filling
    if (hal_ && hal_->hasWaterLevelSensors()) {
        bool high_sensor = hal_->readWaterLevelHigh();
        bool low_sensor = hal_->readWaterLevelLow();
        if (high_sensor || low_sensor) {
            ESP_LOGE(TAG, "Feed refused: tank not empty (HIGH=%d LOW=%d), drain it first", high_sensor, low_sensor);
            return;
        }
    } else {
        ESP_LOGW(TAG, "Feed: no water level sensors - skipping pre-fill check");
    }

    ESP_LOGI(TAG, "Feed: fill -> nutrients (delta volume) -> pH correction");
    auto_ph_correction_pending_ = true;
    is_reservoir_change_ = false;  // normal feed doses for the delta volume (LOW->HIGH)

    if (psm_) {
        psm_->logEvent(psm_event::FEED_OPERATION, 0);  // 0 = STARTED
    }
    transitionTo(ControllerState::FEED_FILLING);
}

void PlantOSController::startEcCheck() {
    if (!startAllowed(Operation::EC_CHECK, "EC check")) return;

    ESP_LOGI(TAG, "Manual EC check");
    ec_pre_feeding_ = 0.0f;
    ec_total_ml_per_L_ = 0.0f;
    ec_attempt_count_ = 0;
    ec_cycle_water_filled_ = false;
    transitionTo(ControllerState::EC_PROCESSING);
}

void PlantOSController::startEcCalibration() {
    if (!startAllowed(Operation::EC_CALIBRATION, "EC calibration")) {
        if (current_state_ == ControllerState::IDLE) {  // refused for the sensor, not the state
            status_logger_.updateAlertStatus("EC_CALIB_ERROR", "EC sensor not available for calibration");
        }
        return;
    }

    ESP_LOGI(TAG, "Starting EC calibration (target %.1f uS/cm)", ec_calib_target_);
    ec_calib_step_ = ECCalibStep::RINSE_PROMPT;
    ec_calib_step_start_time_ = hal_->getSystemTime();
    ec_calib_readings_.clear();
    ec_calib_last_reading_time_ = 0;
    ec_calib_prompt_shown_ = false;
    transitionTo(ControllerState::EC_CALIBRATING);
}

void PlantOSController::startReservoirChange() {
    // The wastewater pump is a Shelly socket - no Shelly, no drain
    if (!startAllowed(Operation::RESERVOIR_CHANGE, "reservoir change")) return;

    ESP_LOGI(TAG, "Reservoir change: drain -> fill -> nutrients (total volume) -> pH correction");
    auto_ph_correction_pending_ = true;
    is_reservoir_change_ = true;  // reservoir change doses for the total volume (EMPTY->HIGH)

    if (psm_) {
        psm_->logEvent(psm_event::RESERVOIR_CHANGE, 0);  // 0 = STARTED
    }
    transitionTo(ControllerState::WATER_EMPTYING);
}

void PlantOSController::emergencyStop() {
    ESP_LOGW(TAG, "EMERGENCY STOP - all pumps off");
    turnOffAllPumps();
    transitionTo(ControllerState::SHUTDOWN);
}

void PlantOSController::setToShutdown() {
    ESP_LOGI(TAG, "SHUTDOWN requested");
    transitionTo(ControllerState::SHUTDOWN);
}

void PlantOSController::setToPause() {
    ESP_LOGI(TAG, "PAUSE requested");
    transitionTo(ControllerState::PAUSE);
}

void PlantOSController::setToIdle() {
    if (current_state_ == ControllerState::SHUTDOWN || current_state_ == ControllerState::PAUSE) {
        ESP_LOGI(TAG, "Resume from %s", stateName(current_state_));

        // Clear PSM persistence
        if (psm_) {
            psm_->clearEvent();
        }
        transitionTo(ControllerState::IDLE);
    } else {
        ESP_LOGW(TAG, "Resume ignored: not in SHUTDOWN/PAUSE (state %s)", stateName(current_state_));
    }
}

void PlantOSController::activateAirPump() {
    // Single forced ON command (WiFi on_connect), unlimited duration, bypasses debouncing
    if (!profile_.has(preset_model::FN_AIR_PUMP)) {
        ESP_LOGD(TAG, "Air pump activation skipped: not in preset %s", profile_.active_name());
        return;
    }
    if (!requestPump(AIR_PUMP, true, 0, true)) {
        ESP_LOGW(TAG, "Air pump activation (WiFi connect) rejected");
    }
}

void PlantOSController::resetToInit() {
    ESP_LOGI(TAG, "Reset to INIT requested");
    turnOffAllPumps();
    transitionTo(ControllerState::INIT);
}

const char *PlantOSController::stateName(ControllerState state) {
    static const char *const NAMES[] = {
        "INIT", "IDLE", "NIGHT", "SHUTDOWN", "PAUSE", "ERROR",
        "PH_PROCESSING", "PH_MEASURING", "PH_CALCULATING", "PH_INJECTING", "PH_MIXING", "PH_CALIBRATING",
        "FEEDING", "WATER_FILLING", "WATER_EMPTYING", "FEED_FILLING",
        "EC_PROCESSING", "EC_CALCULATING", "EC_FEEDING", "EC_MIXING", "EC_MEASURING",
        "EC_CALIBRATING"
    };
    int i = static_cast<int>(state);
    return (i >= 0 && i < static_cast<int>(sizeof(NAMES) / sizeof(NAMES[0]))) ? NAMES[i] : "UNKNOWN";
}

std::string PlantOSController::getStateAsString() const {
    return std::string(stateName(current_state_));
}

// ============================================================================
// State Transition
// ============================================================================

void PlantOSController::transitionTo(ControllerState newState) {
    if (newState == current_state_) {
        return; // Already in this state
    }

    const char* newStateName = stateName(newState);
    ESP_LOGI(TAG, "State %s -> %s (after %us)", stateName(current_state_), newStateName,
             (unsigned)(getStateElapsed() / 1000));

    current_state_ = newState;
    state_start_time_ = esphome::millis();
    state_counter_ = 0;
    state_entry_executed_ = false;  // Reset entry flag for new state
}

uint32_t PlantOSController::getStateElapsed() const {
    return esphome::millis() - state_start_time_;
}

// ============================================================================
// Actuator Control Helpers
// ============================================================================

bool PlantOSController::requestPump(const char *pumpId, bool state, uint32_t durationSec, bool forceExecute) {
    if (!safety_gate_) {
        ESP_LOGW(TAG, "SafetyGate not available - cannot control %s", pumpId);
        return false;
    }

    ESP_LOGD(TAG, "Actuator %s %s for %us%s", pumpId, state ? "ON" : "OFF", (unsigned)durationSec,
             forceExecute ? " (forced)" : "");

    return safety_gate_->executeCommand(pumpId, state, durationSec, forceExecute);
}

bool PlantOSController::requestPumpAdaptive(const char *pumpId, bool state, uint32_t requestedDurationSec, bool forceExecute) {
    // Try original request first
    bool approved = safety_gate_->executeCommand(pumpId, state, requestedDurationSec, forceExecute);
    if (approved) {
        ESP_LOGD(TAG, "Actuator %s %s for %us", pumpId, state ? "ON" : "OFF", (unsigned)requestedDurationSec);
        return true;
    }

    // Request was rejected - analyze rejection type
    bool current_state = safety_gate_->getState(pumpId);

    // Check for debouncing rejection (actuator already in requested state)
    if (current_state == state) {
        // Debouncing rejection - NO RETRY (already executing)
        ESP_LOGD(TAG, "%s debouncing rejection - already %s (no retry)",
                 pumpId, state ? "ON" : "OFF");
        return false;
    }

    // Duration violation - TRY TO ADAPT
    if (state && requestedDurationSec > 0) {
        uint32_t adapted_duration = safety_gate_->getAdaptedDuration(pumpId, requestedDurationSec);

        // Check if adaptation is possible
        if (adapted_duration > 0 && adapted_duration < requestedDurationSec) {
            // Duration can be adapted - log comprehensive error with adaptation
            status_logger_.updateAlertWithContext(
                std::string("DURATION_ADAPTED_") + pumpId,
                "Duration adapted to " + std::to_string(adapted_duration) + "s",
                "SafetyGate rejected: Duration " + std::to_string(requestedDurationSec) +
                    "s exceeds max " + std::to_string(adapted_duration) + "s",
                "System adapted duration automatically. Consider increasing max duration in config or reducing dose.",
                "State " + getStateAsString(),
                "Retrying with adapted duration " + std::to_string(adapted_duration) + "s",
                0
            );

            // Retry with adapted duration
            approved = safety_gate_->executeCommand(pumpId, state, adapted_duration, forceExecute);

            if (approved) {
                // Adaptation successful - mark alert as resolved
                ESP_LOGW(TAG, "%s duration cut by SafetyGate: %us -> %us", pumpId,
                         (unsigned)requestedDurationSec, (unsigned)adapted_duration);
                status_logger_.resolveAlert(std::string("DURATION_ADAPTED_") + pumpId);
                return true;
            } else {
                // Even adapted duration was rejected (should not happen)
                ESP_LOGE(TAG, "%s rejected even after adaptation to %us", pumpId, (unsigned)adapted_duration);
            }
        }
    }

    // Could not adapt or adaptation failed - comprehensive error
    std::string rejection_reason = std::string("SafetyGate rejected ") + pumpId + " command";
    if (state && requestedDurationSec > 0) {
        uint32_t max_duration = safety_gate_->getMaxDurationSeconds(pumpId);
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
        std::string("SAFETY_GATE_REJECT_") + pumpId,
        rejection_reason,
        std::string("SafetyGate rejected command: ") + pumpId + " " + (state ? "ON" : "OFF") +
            " for " + std::to_string(requestedDurationSec) + "s",
        "Check SafetyGate configuration, verify pump is not stuck, check duration limits in plantOS.yaml",
        "State " + getStateAsString(),
        "Operation aborted. Manual intervention may be required.",
        0
    );

    return false;
}

bool PlantOSController::requestValve(const char *valveId, bool state, uint32_t durationSec) {
    if (!safety_gate_) {
        ESP_LOGW(TAG, "SafetyGate not available - cannot control %s", valveId);
        return false;
    }

    ESP_LOGD(TAG, "Valve %s %s for %us", valveId, state ? "OPEN" : "CLOSED", (unsigned)durationSec);

    return safety_gate_->executeCommand(valveId, state, durationSec);
}

void PlantOSController::turnOffAllPumps() {
    ESP_LOGD(TAG, "All pumps and valves OFF");
    cancelActuatorTests();  // the OFF commands below end any running test

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
            ESP_LOGD(TAG, "AirPump Shelly sequence stopped - pump OFF");
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

    return hal_->readPH();
}

bool PlantOSController::hasPhValue() {
    if (!hal_) {
        return false;
    }
    return hal_->hasPhValue();
}

// ============================================================================
// Automation toggles
// ============================================================================

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

void PlantOSController::setAutoReservoirChangeEnabled(bool enabled) {
    auto_reservoir_change_enabled_ = enabled;

    if (psm_) {
        psm_->saveState(NVS_KEY_AUTO_RES_ENABLE, enabled);
    }

    ESP_LOGI(TAG, "Auto reservoir change %s", enabled ? "ENABLED" : "DISABLED");
}

void PlantOSController::setAutoReservoirChangeDay(uint8_t day) {
    if (day > 6) {
        ESP_LOGW(TAG, "Weekday %d invalid, using 6", day);
        day = 6;
    }

    auto_reservoir_change_day_ = day;

    if (psm_) {
        psm_->saveState(NVS_KEY_AUTO_RES_DAY, static_cast<int>(day));
    }

    ESP_LOGI(TAG, "Auto reservoir change weekday: %d (0=Sun)", day);
}

} // namespace plantos_controller
