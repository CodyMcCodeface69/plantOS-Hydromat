#include "grow_profile.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace plantos_controller {

using namespace preset_model;

static const char *const TAG = "grow_profile";

// NVS keys (hashed with fnv1_hash). Index = preset index.
static const char *const NVS_KEY_ACTIVE = "preset_active";
static const char *const NVS_KEY_VALUES[PRESET_COUNT] = {"preset_slot_0", "preset_slot_1", "preset_slot_2"};

GrowProfile::GrowProfile() {
  for (uint8_t i = 0; i < PRESET_COUNT; i++) {
    values_[i] = DEFAULT_VALUES[i];
  }
}

void GrowProfile::set_defaults(uint8_t index, float delta_l, float total_l, uint8_t mask) {
  if (index >= PRESET_COUNT) return;
  values_[index] = PresetValues{delta_l, total_l, static_cast<uint8_t>(mask & FN_ALL)};
}

void GrowProfile::load() {
  active_pref_ = esphome::global_preferences->make_preference<ActiveRecord>(esphome::fnv1_hash(NVS_KEY_ACTIVE), true);
  for (uint8_t i = 0; i < PRESET_COUNT; i++) {
    value_prefs_[i] =
        esphome::global_preferences->make_preference<PresetRecord>(esphome::fnv1_hash(NVS_KEY_VALUES[i]), true);
    PresetRecord rec{};
    PresetValues v{};
    stored_[i] = false;
    if (value_prefs_[i].load(&rec)) {
      if (fromRecord(rec, &v)) {
        values_[i] = v;
        stored_[i] = true;
      } else {
        ESP_LOGW(TAG, "Invalid NVS record for preset %s - using YAML defaults", presetName(i));
      }
    }
  }
  ActiveRecord act{};
  active_ = active_pref_.load(&act) ? activeFromRecord(act, HAUPT) : HAUPT;
  prefs_ready_ = true;
}

bool GrowProfile::set_active(uint8_t index) {
  if (index >= PRESET_COUNT) return false;
  if (index == active_) return true;
  active_ = index;
  if (prefs_ready_) {
    ActiveRecord rec{RECORD_VERSION, index, 0};
    if (!active_pref_.save(&rec)) {
      ESP_LOGE(TAG, "Failed to save the active preset to NVS");
    }
  }
  return true;
}

bool GrowProfile::set_current(const PresetValues &v) {
  if (!volumesValid(v.delta_l, v.total_l) || (v.mask & ~FN_ALL) != 0) return false;
  if (sameValues(v, values_[active_])) return true;
  values_[active_] = v;
  return save_values_(active_);
}

bool GrowProfile::save_values_(uint8_t index) {
  if (!prefs_ready_) return true;  // before load(): RAM only (host tests without setup)
  PresetRecord rec = toRecord(values_[index]);
  if (!value_prefs_[index].save(&rec)) {
    ESP_LOGE(TAG, "Failed to save preset %s to NVS", presetName(index));
    return false;
  }
  stored_[index] = true;
  return true;
}

}  // namespace plantos_controller
