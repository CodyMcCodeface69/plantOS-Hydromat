// Status report: "Print Status Report" button (requestStatusReport()) and the optional 60 s
// periodic report (debug switch "System Status Reports", setPeriodicStatusReport()).
// Everything is read from the controller, the HAL, the calendar and the PSM at print time;
// only alerts and the I2C scan live in CentralStatusLogger. One log line per topic.
// Part of PlantOSController (declared in controller.h).
#include "controller.h"
#include "esphome/components/plantos_hal/hal.h"
#include "esphome/components/persistent_state_manager/persistent_state_manager.h"
#include "esphome/components/calendar_manager/CalendarManager.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include <esp_system.h>
#endif

namespace plantos_controller {

namespace {

// "6.12" or "-" when there is no valid value
void fmtValue(char *buf, size_t size, bool valid, float value, const char *fmt) {
    if (valid && !std::isnan(value)) {
        snprintf(buf, size, fmt, value);
    } else {
        snprintf(buf, size, "-");
    }
}

// Tank level from the three sensors (true = water at the sensor)
const char *levelName(bool high, bool low, bool empty) {
    if (high && low) return "FULL";
    if (!high && low) return "NORMAL";
    if (!high && !low && empty) return "LOW (auto-feed zone)";
    if (!high && !low && !empty) return "EMPTY (below EMPTY sensor)";
    return "ERROR (HIGH without LOW, check wiring)";
}

}  // namespace

void PlantOSController::setPeriodicStatusReport(bool enabled) {
    status_report_periodic_ = enabled;
    last_status_report_ms_ = esphome::millis();
    ESP_LOGI(TAG, "Periodic status report %s", enabled ? "every 60 s" : "off");
}

void PlantOSController::updateStatusReport() {
    uint32_t now = esphome::millis();
    bool periodic_due = status_report_periodic_ && now - last_status_report_ms_ >= STATUS_REPORT_INTERVAL_MS;
    if (!status_report_requested_ && !periodic_due) {
        return;
    }
    status_report_requested_ = false;
    last_status_report_ms_ = now;
    printStatusReport();
}

void PlantOSController::printStatusReport() {
    char a[24], b[24], c[24], d[24];

    // Header: local time, uptime, heap, IP
    char when[24] = "time not synced";
    time_t t = time(nullptr);
    struct tm *tm = localtime(&t);
    if (tm && tm->tm_year >= (2020 - 1900)) {
        strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", tm);
    }
    uint32_t heap_free = 0, heap_min = 0, heap_block = 0;
#ifdef ESP_PLATFORM
    heap_free = esp_get_free_heap_size();
    heap_min = esp_get_minimum_free_heap_size();
    heap_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#endif
    ESP_LOGI(TAG, "=== PlantOS status %s | up %s | heap %u free, %u min, %u block | IP %s ===", when,
             uptimeText().c_str(), (unsigned) heap_free, (unsigned) heap_min, (unsigned) heap_block,
             wifi_ip_[0] ? wifi_ip_ : "-");
    if (tm && (tm->tm_hour == 4 || tm->tm_hour == 16) && tm->tm_min == 20 && tm->tm_year >= (2020 - 1900)) {
        ESP_LOGI(TAG, "4:20 - all plants say hi");
    }

    // State, preset, calendar
    ESP_LOGI(TAG, "State: %s for %u s | %s", stateName(current_state_), (unsigned) (getStateElapsed() / 1000),
             presetStatusText().c_str());
    if (calendar_manager_) {
        auto s = calendar_manager_->get_today_schedule();
        if (s.ec_target > 0.0f) {
            snprintf(a, sizeof(a), "%.0f x %.2f = %.0f", s.ec_target, ec_target_multiplier_,
                     s.ec_target * ec_target_multiplier_);
        } else {
            snprintf(a, sizeof(a), "off today");
        }
        ESP_LOGI(TAG, "Calendar: pH %.1f-%.1f | A %.2f B %.2f C %.2f mL/L | EC target %s uS/cm | auto %s",
                 s.target_ph_min, s.target_ph_max, s.nutrient_A_ml_per_liter, s.nutrient_B_ml_per_liter,
                 s.nutrient_C_ml_per_liter, a, calendar_manager_->is_safe_mode() ? "OFF" : "ON");
    } else {
        ESP_LOGW(TAG, "Calendar: not configured");
    }

    // Sensors ("-" = no valid reading)
    fmtValue(a, sizeof(a), hal_->hasPhValue(), hal_->hasPhValue() ? hal_->readPH() : NAN, "%.2f");
    fmtValue(b, sizeof(b), hal_->hasECValue(), hal_->hasECValue() ? hal_->readEC() : NAN, "%.0f uS/cm");
    fmtValue(c, sizeof(c), hal_->hasTemperature(), hal_->hasTemperature() ? hal_->readTemperature() : NAN, "%.1f C");
    char level[64] = "-";
    if (hal_->hasWaterLevelSensors()) {
        bool high = hal_->readWaterLevelHigh(), low = hal_->readWaterLevelLow(), empty = hal_->readWaterLevelEmpty();
        snprintf(level, sizeof(level), "H/L/E %d/%d/%d %s", high, low, empty, levelName(high, low, empty));
    }
    float air_t, air_h, air_p;
    hal_->readAir(air_t, air_h, air_p);
    fmtValue(d, sizeof(d), true, air_t, "%.1f C");
    char air[48];
    snprintf(air, sizeof(air), "%s", d);
    size_t n = strlen(air);
    if (!std::isnan(air_h) && n < sizeof(air)) n += snprintf(air + n, sizeof(air) - n, " %.0f %%", air_h);
    if (!std::isnan(air_p) && n < sizeof(air)) snprintf(air + n, sizeof(air) - n, " %.0f hPa", air_p);
    ESP_LOGI(TAG, "Sensors: pH %s | EC %s | water %s | level %s | air %s", a, b, c, level, air);

    // Actuators as the HAL last switched them (1 = on)
    ESP_LOGI(TAG, "Actuators: acid %d A %d B %d C %d valve %d | air %d drain %d light %d",
             hal_->getPumpState("AcidPump"), hal_->getPumpState("NutrientPumpA"), hal_->getPumpState("NutrientPumpB"),
             hal_->getPumpState("NutrientPumpC"), hal_->getValveState(WATER_VALVE), hal_->getPumpState(AIR_PUMP),
             hal_->getPumpState(WASTEWATER_PUMP), hal_->getPumpState("GrowLight"));

    // Hardware
    char i2c[96];
    status_logger_.formatI2C(i2c, sizeof(i2c));
    if (!hal_->isShellyEnabled()) {
        snprintf(a, sizeof(a), "disabled");
    } else if (hal_->isShellyReachable()) {
        uint32_t up = hal_->getShellyUptime();
        snprintf(a, sizeof(a), "online, up %uh%02um", (unsigned) (up / 3600), (unsigned) ((up % 3600) / 60));
    } else {
        snprintf(a, sizeof(a), "OFFLINE");
    }
    ESP_LOGI(TAG, "Hardware: EZO pH %s | DS18B20 %s | level sensors %s | %s | Shelly %s",
             hal_->isPhSensorReady() ? "ok" : "NOT READY", hal_->hasTemperature() ? "ok" : "no reading",
             hal_->hasWaterLevelSensors() ? "ok" : "MISSING", i2c, a);

    // Learned factors
    ESP_LOGI(TAG, "K: pH %.4f | EC feed %.6f | EC calibration %.3f", ph_K_, ec_K_feed_,
             hal_->getECCalibrationFactor());

    // PSM event, last refusal
    if (psm_ && psm_->hasEvent()) {
        auto event = psm_->getLastEvent();
        ESP_LOGI(TAG, "PSM: %s (status %d, %lld s)", event.eventID, event.status, (long long) psm_->getEventAge());
    } else {
        ESP_LOGI(TAG, "PSM: -");
    }
    ESP_LOGI(TAG, "Last refusal: %s", lastRefusalText().c_str());

    status_logger_.logActiveAlerts(TAG);
}

}  // namespace plantos_controller
