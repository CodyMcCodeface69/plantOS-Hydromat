// Implementation of the host stubs: fake clocks, fake NVS, log sink.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "esphome/components/time/real_time_clock.h"

namespace host {

static uint32_t g_millis = 0;
static int64_t g_unix_time = 0;

bool log_enabled = getenv("PLANTOS_TEST_LOG") != nullptr;
std::map<uint32_t, std::vector<uint8_t>> nvs;
bool nvs_fail_writes = false;
int nvs_save_count = 0;

void set_millis(uint32_t ms) { g_millis = ms; }
void advance_millis(uint32_t ms) { g_millis += ms; }
void set_unix_time(int64_t t) { g_unix_time = t; }
void reset() {
  g_millis = 0;
  g_unix_time = 0;
  nvs.clear();
  nvs_fail_writes = false;
  nvs_save_count = 0;
}

void log_line(char level, const char *tag, const char *fmt, ...) {
  if (!log_enabled) return;
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "    [%c][%s] ", level, tag);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
}

}  // namespace host

// libc time() is replaced for the whole test binary, so components calling
// time(nullptr) (persistent_state_manager, CentralStatusLogger) are deterministic.
extern "C" time_t time(time_t *out) noexcept {
  time_t t = static_cast<time_t>(host::g_unix_time);
  if (out) *out = t;
  return t;
}

namespace esphome {

uint32_t millis() { return host::g_millis; }
uint32_t micros() { return host::g_millis * 1000u; }
void delay(uint32_t ms) { host::g_millis += ms; }
void delayMicroseconds(uint32_t) {}

static ESPPreferences g_prefs;
ESPPreferences *global_preferences = &g_prefs;

namespace time {

// days_from_civil (H. Hinnant); independent of the copy in controller.cpp on purpose.
static int64_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

void RealTimeClock::set_local(int year, int month, int day, int hour, int minute, int second, int utc_offset_s) {
  int64_t days = days_from_civil(year, month, day);
  ESPTime t;
  t.year = year;
  t.month = month;
  t.day_of_month = day;
  t.hour = hour;
  t.minute = minute;
  t.second = second;
  t.day_of_week = static_cast<uint8_t>(((days % 7 + 11) % 7) + 1);  // 1970-01-01 = Thursday = 5
  t.day_of_year = static_cast<uint16_t>(days - days_from_civil(year, 1, 1) + 1);
  t.timestamp = static_cast<time_t>(days * 86400 + hour * 3600 + minute * 60 + second - utc_offset_s);
  now_ = t;
}

}  // namespace time
}  // namespace esphome
