#pragma once
// Runtime presets (Anzucht / Mutter / Haupt): the pure part of the model.
// No ESPHome includes: the host tests call these functions directly.
//
// A preset is {tank volume LOW->HIGH, tank volume EMPTY->HIGH, function mask}. Its calendar
// lives in calendar_manager (same index), its NVS record in grow_profile.cpp.
// FSMINFO.md "Presets" describes the behavior built on top of this.
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace plantos_controller {
namespace preset_model {

// Preset indices = calendar indices (calendar_manager::CAL_ANZUCHT / CAL_MUTTER / CAL_HAUPT)
constexpr uint8_t ANZUCHT = 0;
constexpr uint8_t MUTTER = 1;
constexpr uint8_t HAUPT = 2;
constexpr uint8_t PRESET_COUNT = 3;

// Function bits. pH measurement and correction (probe + acid pump) are always available.
constexpr uint8_t FN_NUTRIENTS = 1u << 0;   // nutrient pumps A/B/C: feeding, EC feeding, mother mix
constexpr uint8_t FN_WATER_FILL = 1u << 1;  // water valve + level sensors: fill, top-up, feed fill
constexpr uint8_t FN_DRAIN = 1u << 2;       // wastewater pump (Shelly 2): empty tank, reservoir change
constexpr uint8_t FN_AIR_PUMP = 1u << 3;    // air pump (Shelly 0): aeration / mixing
constexpr uint8_t FN_GROW_LIGHT = 1u << 4;  // grow light (Shelly 3): light schedule
constexpr uint8_t FN_ALL = 0x1F;
constexpr uint8_t FN_COUNT = 5;

// UI range of the tank volume numbers (liters)
constexpr float VOLUME_MIN_L = 1.0f;
constexpr float VOLUME_MAX_L = 300.0f;

struct PresetValues {
  float delta_l;  // LOW -> HIGH (daily feed, pH dose, mixing time)
  float total_l;  // EMPTY -> HIGH (reservoir change)
  uint8_t mask;   // FN_* bits
};

// Built-in defaults, identical to the defaults of the `presets:` schema in __init__.py
// (the YAML block in plantOS.yaml is the one place to change them).
constexpr PresetValues DEFAULT_VALUES[PRESET_COUNT] = {
    {5.0f, 8.0f, 0},                                        // Anzucht: pH only (placeholder volumes)
    {10.0f, 15.0f, FN_NUTRIENTS | FN_WATER_FILL | FN_DRAIN},  // Mutter (placeholder volumes)
    {27.4f, 47.5f, FN_ALL},                                 // Haupt: production
};

inline const char *presetName(uint8_t index) {
  static const char *const NAMES[PRESET_COUNT] = {"Anzucht", "Mutter", "Haupt"};
  return index < PRESET_COUNT ? NAMES[index] : "?";
}

/// Index for a preset name ("Anzucht" / "Mutter" / "Haupt"), PRESET_COUNT if unknown
inline uint8_t presetIndex(const char *name) {
  if (name == nullptr) return PRESET_COUNT;
  for (uint8_t i = 0; i < PRESET_COUNT; i++) {
    const char *a = presetName(i);
    const char *b = name;
    while (*a && *a == *b) {
      a++;
      b++;
    }
    if (*a == 0 && *b == 0) return i;
  }
  return PRESET_COUNT;
}

/// UI name of one function bit ("Nutrients", "Water Fill", "Drain", "Air Pump", "Grow Light")
inline const char *functionName(uint8_t bit) {
  switch (bit) {
    case FN_NUTRIENTS: return "Nutrients";
    case FN_WATER_FILL: return "Water Fill";
    case FN_DRAIN: return "Drain";
    case FN_AIR_PUMP: return "Air Pump";
    case FN_GROW_LIGHT: return "Grow Light";
    default: return "?";
  }
}

/// "Nutrients + Water Fill" for a mask ("none" for 0). Returns buf.
inline const char *formatFunctions(char *buf, size_t n, uint8_t mask) {
  if (n == 0) return buf;
  buf[0] = 0;
  size_t len = 0;
  for (uint8_t i = 0; i < FN_COUNT; i++) {
    uint8_t bit = 1u << i;
    if (!(mask & bit)) continue;
    int w = snprintf(buf + len, n - len, "%s%s", len ? " + " : "", functionName(bit));
    if (w < 0 || static_cast<size_t>(w) >= n - len) break;
    len += static_cast<size_t>(w);
  }
  if (len == 0) snprintf(buf, n, "none");
  return buf;
}

/// Functions of `required` that `available` lacks (0 = can run)
constexpr uint8_t missingFunctions(uint8_t required, uint8_t available) {
  return static_cast<uint8_t>(required & ~available);
}

/// Function an actuator belongs to; 0 = always available (AcidPump) or unknown id
inline uint8_t actuatorFunction(const char *id) {
  struct Map {
    const char *id;
    uint8_t fn;
  };
  static const Map MAP[] = {
      {"NutrientPumpA", FN_NUTRIENTS}, {"NutrientPumpB", FN_NUTRIENTS}, {"NutrientPumpC", FN_NUTRIENTS},
      {"WaterValve", FN_WATER_FILL},   {"WastewaterPump", FN_DRAIN},    {"AirPump", FN_AIR_PUMP},
      {"GrowLight", FN_GROW_LIGHT},
  };
  if (id == nullptr) return 0;
  for (const auto &m : MAP) {
    const char *a = m.id;
    const char *b = id;
    while (*a && *a == *b) {
      a++;
      b++;
    }
    if (*a == 0 && *b == 0) return m.fn;
  }
  return 0;
}

inline bool volumeInRange(float liters) { return liters >= VOLUME_MIN_L && liters <= VOLUME_MAX_L; }

/// Valid pair: both in range and LOW->HIGH not larger than EMPTY->HIGH
inline bool volumesValid(float delta_l, float total_l) {
  return volumeInRange(delta_l) && volumeInRange(total_l) && delta_l <= total_l;
}

// ---- NVS records -------------------------------------------------------------------------
// Layout is the NVS format: largest members first (RISC-V alignment, docs/LESSONS.md),
// do not reorder. A record that fails validation is ignored (YAML defaults apply).

constexpr uint16_t RECORD_VERSION = 1;

struct PresetRecord {
  float delta_l;
  float total_l;
  uint16_t version;  // RECORD_VERSION
  uint8_t mask;
  uint8_t reserved;  // 0
};
static_assert(sizeof(PresetRecord) == 12, "preset NVS record layout changed");

struct ActiveRecord {
  uint16_t version;  // RECORD_VERSION
  uint8_t index;
  uint8_t reserved;  // 0
};
static_assert(sizeof(ActiveRecord) == 4, "active preset NVS record layout changed");

inline PresetRecord toRecord(const PresetValues &v) { return PresetRecord{v.delta_l, v.total_l, RECORD_VERSION, v.mask, 0}; }

/// true and *out filled if `rec` is a valid record of this version
inline bool fromRecord(const PresetRecord &rec, PresetValues *out) {
  if (rec.version != RECORD_VERSION || (rec.mask & ~FN_ALL) != 0) return false;
  if (!(rec.delta_l == rec.delta_l) || !(rec.total_l == rec.total_l)) return false;  // NaN
  if (!volumesValid(rec.delta_l, rec.total_l)) return false;
  *out = PresetValues{rec.delta_l, rec.total_l, rec.mask};
  return true;
}

/// Active index from a stored record; `fallback` if the record is invalid
inline uint8_t activeFromRecord(const ActiveRecord &rec, uint8_t fallback) {
  return (rec.version == RECORD_VERSION && rec.index < PRESET_COUNT) ? rec.index : fallback;
}

/// Values differ enough to be worth an NVS write (numbers publish floats like 27.399999)
inline bool sameValues(const PresetValues &a, const PresetValues &b) {
  auto close = [](float x, float y) { return (x > y ? x - y : y - x) < 0.005f; };
  return a.mask == b.mask && close(a.delta_l, b.delta_l) && close(a.total_l, b.total_l);
}

// ---- texts -------------------------------------------------------------------------------

/// "Haupt · Day 69/120 · Bloom (pH 6.0-6.4, EC 2.20)" (EC in mS/cm; "EC off" when the target
/// is 0; "Day 69" when defined_days is 0; no phase part without a phase name). Returns buf.
inline const char *formatPresetStatus(char *buf, size_t n, const char *preset, uint16_t day, uint16_t defined_days,
                                      const char *phase, float ph_min, float ph_max, float ec_target_us_cm) {
  char day_s[16];
  if (defined_days > 0) {
    snprintf(day_s, sizeof(day_s), "Day %u/%u", (unsigned) day, (unsigned) defined_days);
  } else {
    snprintf(day_s, sizeof(day_s), "Day %u", (unsigned) day);
  }
  char ec_s[16];
  if (ec_target_us_cm > 0.0f) {
    snprintf(ec_s, sizeof(ec_s), "EC %.2f", ec_target_us_cm / 1000.0f);
  } else {
    snprintf(ec_s, sizeof(ec_s), "EC off");
  }
  if (phase != nullptr && phase[0] != 0) {
    snprintf(buf, n, "%s \xC2\xB7 %s \xC2\xB7 %s (pH %.1f-%.1f, %s)", preset, day_s, phase, ph_min, ph_max, ec_s);
  } else {
    snprintf(buf, n, "%s \xC2\xB7 %s (pH %.1f-%.1f, %s)", preset, day_s, ph_min, ph_max, ec_s);
  }
  return buf;
}

/// "Day 69 / 120" (the grow day text sensor; "Day 69" for an open-ended calendar). Returns buf.
inline const char *formatGrowDay(char *buf, size_t n, uint16_t day, uint16_t defined_days) {
  if (defined_days > 0) {
    snprintf(buf, n, "Day %u / %u", (unsigned) day, (unsigned) defined_days);
  } else {
    snprintf(buf, n, "Day %u", (unsigned) day);
  }
  return buf;
}

}  // namespace preset_model
}  // namespace plantos_controller
