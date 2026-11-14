// Host stub for esphome/core/preferences.h: an in-memory NVS.
// host::reset() wipes it; host::nvs_fail_writes makes save() return false.
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>
#include "esphome/core/helpers.h"

namespace host {
extern std::map<uint32_t, std::vector<uint8_t>> nvs;
extern bool nvs_fail_writes;
extern int nvs_save_count;
}  // namespace host

namespace esphome {

class ESPPreferenceObject {
 public:
  ESPPreferenceObject() = default;
  ESPPreferenceObject(uint32_t key, size_t size) : key_(key), size_(size), valid_(true) {}

  template<typename T> bool save(const T *src) {
    if (!valid_ || host::nvs_fail_writes) return false;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(src);
    host::nvs[key_] = std::vector<uint8_t>(p, p + sizeof(T));
    host::nvs_save_count++;
    return true;
  }

  template<typename T> bool load(T *dest) {
    if (!valid_) return false;
    auto it = host::nvs.find(key_);
    if (it == host::nvs.end() || it->second.size() != sizeof(T)) return false;
    memcpy(reinterpret_cast<void *>(dest), it->second.data(), sizeof(T));
    return true;
  }

 private:
  uint32_t key_{0};
  size_t size_{0};
  bool valid_{false};
};

class ESPPreferences {
 public:
  template<typename T> ESPPreferenceObject make_preference(uint32_t type, bool in_flash = false) {
    (void) in_flash;
    return ESPPreferenceObject(type, sizeof(T));
  }
  bool sync() { return !host::nvs_fail_writes; }
  bool reset() {
    host::nvs.clear();
    return true;
  }
};

extern ESPPreferences *global_preferences;

}  // namespace esphome
