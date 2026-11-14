// Host stub for esphome/components/time/real_time_clock.h.
// Tests set the wall clock with set_local(); now() returns it unchanged.
#pragma once
#include "esphome/core/component.h"
#include "esphome/core/time.h"

namespace esphome {
namespace time {

class RealTimeClock : public PollingComponent {
 public:
  ESPTime now() { return now_; }
  ESPTime utcnow() { return now_; }
  time_t timestamp_now() { return now_.timestamp; }

  // --- test control ---
  // Local civil time plus the UTC offset it was observed at. The timestamp is
  // derived (local - offset), like SNTP + TZ would produce on the device.
  void set_local(int year, int month, int day, int hour, int minute, int second = 0, int utc_offset_s = 0);
  void set_invalid() { now_ = ESPTime{}; }
  void set_raw(const ESPTime &t) { now_ = t; }

 private:
  ESPTime now_{};
};

}  // namespace time
}  // namespace esphome
