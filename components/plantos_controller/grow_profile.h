#pragma once
// GrowProfile: the preset store. Active preset index plus per preset {tank volumes, function mask},
// YAML defaults, one NVS record per preset and one for the active index (written only on change).
// The calendar of a preset is calendar_manager's calendar with the same index; its grow day is
// kept there. Owned by PlantOSController (no separate ESPHome component: the controller is the
// only user, and the YAML defaults are part of the `plantos_controller: presets:` block).
#include "preset_model.h"
#include "esphome/core/preferences.h"

namespace plantos_controller {

class GrowProfile {
 public:
  GrowProfile();

  /// YAML default for one preset (before load())
  void set_defaults(uint8_t index, float delta_l, float total_l, uint8_t mask);

  /// Read the active index and the preset records from NVS. Missing or invalid -> defaults
  /// (first boot of this firmware: Haupt with the YAML values, i.e. today's behavior).
  void load();

  uint8_t active() const { return active_; }
  const char *active_name() const { return preset_model::presetName(active_); }
  const preset_model::PresetValues &values(uint8_t index) const { return values_[index]; }
  const preset_model::PresetValues &current() const { return values_[active_]; }
  bool has(uint8_t fn) const { return (values_[active_].mask & fn) == fn; }

  /// Make `index` active and persist it. false for an invalid index.
  bool set_active(uint8_t index);
  /// Replace the active preset's values; persisted only if they differ. false if invalid.
  bool set_current(const preset_model::PresetValues &v);
  /// true if `index` has an NVS record (edited in the UI); false = YAML defaults
  bool is_stored(uint8_t index) const { return stored_[index]; }

 private:
  bool save_values_(uint8_t index);

  uint8_t active_{preset_model::HAUPT};
  preset_model::PresetValues values_[preset_model::PRESET_COUNT];
  bool stored_[preset_model::PRESET_COUNT]{false, false, false};
  esphome::ESPPreferenceObject active_pref_;
  esphome::ESPPreferenceObject value_prefs_[preset_model::PRESET_COUNT];
  bool prefs_ready_{false};
};

}  // namespace plantos_controller
