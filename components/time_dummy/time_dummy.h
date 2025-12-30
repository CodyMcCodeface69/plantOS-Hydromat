#pragma once

#include "esphome/core/component.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/log.h"

namespace esphome {
namespace time_dummy {

/**
 * TimeDummy - Configurable Dummy Time Source for Testing
 *
 * Provides a RealTimeClock implementation that uses configurable dummy time
 * instead of NTP synchronization. Useful for testing calendar-based functionality.
 *
 * Features:
 * - Configurable initial date/time at boot (via YAML)
 * - Non-blocking clock that counts forward from initial time
 * - Manual adjustment methods (add/subtract days/hours)
 * - Compatible with ESPHome time::RealTimeClock interface
 *
 * Usage:
 * time_dummy:
 *   id: dummy_time
 *   initial_time: "2025-12-30 08:00:00"
 *
 * The clock starts at the specified time and counts forward in real-time.
 * Time can be adjusted via buttons or services.
 */
class TimeDummy : public time::RealTimeClock {
public:
    TimeDummy() = default;

    /**
     * ESPHome Component setup
     * Initializes the dummy clock with configured initial time
     */
    void setup() override;

    /**
     * ESPHome PollingComponent update
     * Called periodically to update the internal time representation
     */
    void update() override;

    /**
     * Get component name for logging
     */
    float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

    /**
     * Set initial timestamp (Unix seconds since epoch)
     * Called from Python code generation with parsed initial_time value
     * @param timestamp Unix timestamp at boot (seconds since epoch)
     */
    void set_initial_timestamp(int64_t timestamp) {
        initial_timestamp_ = timestamp;
    }

    /**
     * Add specified number of days to current time
     * @param days Number of days to add (can be negative to subtract)
     */
    void add_days(int32_t days);

    /**
     * Add specified number of hours to current time
     * @param hours Number of hours to add (can be negative to subtract)
     */
    void add_hours(int32_t hours);

    /**
     * Get current dummy timestamp
     * @return Current Unix timestamp (seconds since epoch)
     */
    int64_t get_current_timestamp();

protected:
    /**
     * Read time from dummy source
     * Implements RealTimeClock virtual method
     * @return true always (dummy time is always "synchronized")
     */
    bool read_time();

private:
    static constexpr const char* TAG = "time_dummy";

    // Initial timestamp set via YAML (Unix seconds)
    int64_t initial_timestamp_{0};

    // Time when component was initialized (millis())
    uint32_t boot_millis_{0};

    // Manual adjustment offset (seconds)
    // Used when time is adjusted via add_days() or add_hours()
    int64_t adjustment_offset_{0};

    /**
     * Calculate current timestamp based on elapsed time since boot
     * current_time = initial_timestamp + adjustment_offset + (millis() - boot_millis) / 1000
     * @return Current Unix timestamp
     */
    int64_t calculate_current_timestamp();
};

} // namespace time_dummy
} // namespace esphome
