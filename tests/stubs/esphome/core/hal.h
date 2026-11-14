// Host stub for esphome/core/hal.h. Time is fully under test control.
#pragma once
#include <cstdint>

namespace esphome {

uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);          // advances the fake clock, never sleeps
void delayMicroseconds(uint32_t us);
inline void yield() {}

}  // namespace esphome

// Test-side control of the fake clocks (see stubs/host_stubs.cpp).
namespace host {
void set_millis(uint32_t ms);
void advance_millis(uint32_t ms);
void set_unix_time(int64_t t);    // what libc time() returns inside the test binary
void reset();                     // millis = 0, unix time = 0, NVS wiped
}  // namespace host
