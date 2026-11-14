// Host stub for esphome/core/time.h: field layout and is_valid() as in ESPHome 2025.4.
#pragma once
#include <cstdint>
#include <ctime>

namespace esphome {

struct ESPTime {
  uint8_t second{0};        // 0-59
  uint8_t minute{0};        // 0-59
  uint8_t hour{0};          // 0-23
  uint8_t day_of_week{0};   // 1-7, sunday = 1
  uint8_t day_of_month{0};  // 1-31
  uint16_t day_of_year{0};  // 1-366
  uint8_t month{0};         // 1-12
  uint16_t year{0};
  bool is_dst{false};
  time_t timestamp{0};      // UTC epoch seconds

  bool fields_in_range() const {
    return this->second < 61 && this->minute < 60 && this->hour < 24 && this->day_of_week > 0 &&
           this->day_of_week < 8 && this->day_of_month > 0 && this->day_of_month < 32 && this->day_of_year > 0 &&
           this->day_of_year < 367 && this->month > 0 && this->month < 13;
  }
  bool is_valid() const { return this->year >= 2019 && this->fields_in_range(); }
};

}  // namespace esphome
