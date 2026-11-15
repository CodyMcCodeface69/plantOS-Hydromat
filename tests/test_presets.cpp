// Runtime presets, part 1: the pure model (preset_model.h) and the NVS store (GrowProfile).
// Real code under test: components/plantos_controller/{preset_model.h,grow_profile.{h,cpp}}
#include <cstring>
#include <string>
#include "minitest.h"
#include "esphome/core/preferences.h"
#include "esphome/components/plantos_controller/grow_profile.h"
#include "esphome/components/plantos_controller/preset_model.h"

using namespace plantos_controller::preset_model;
using plantos_controller::GrowProfile;

TEST(preset_model, names_and_indices) {
  CHECK_EQ(std::string(presetName(ANZUCHT)), std::string("Anzucht"));
  CHECK_EQ(std::string(presetName(MUTTER)), std::string("Mutter"));
  CHECK_EQ(std::string(presetName(HAUPT)), std::string("Haupt"));
  CHECK_EQ(presetIndex("Haupt"), HAUPT);
  CHECK_EQ(presetIndex("Mutter"), MUTTER);
  CHECK_EQ(presetIndex("Anzucht"), ANZUCHT);
  CHECK_EQ(presetIndex("Haup"), PRESET_COUNT);
  CHECK_EQ(presetIndex("Hauptx"), PRESET_COUNT);
  CHECK_EQ(presetIndex(nullptr), PRESET_COUNT);
}

TEST(preset_model, defaults_match_the_brief) {
  CHECK_EQ(DEFAULT_VALUES[HAUPT].mask, FN_ALL);
  CHECK_NEAR(DEFAULT_VALUES[HAUPT].delta_l, 27.4f, 1e-6);
  CHECK_NEAR(DEFAULT_VALUES[HAUPT].total_l, 47.5f, 1e-6);
  CHECK_EQ(DEFAULT_VALUES[MUTTER].mask, FN_NUTRIENTS | FN_WATER_FILL | FN_DRAIN);
  CHECK_EQ(DEFAULT_VALUES[ANZUCHT].mask, 0);
}

TEST(preset_model, function_texts) {
  char buf[96];
  CHECK_EQ(std::string(formatFunctions(buf, sizeof(buf), 0)), std::string("none"));
  CHECK_EQ(std::string(formatFunctions(buf, sizeof(buf), FN_DRAIN)), std::string("Drain"));
  CHECK_EQ(std::string(formatFunctions(buf, sizeof(buf), FN_ALL)),
           std::string("Nutrients + Water Fill + Drain + Air Pump + Grow Light"));
  char tiny[8];
  formatFunctions(tiny, sizeof(tiny), FN_ALL);  // truncates, never overflows
  CHECK(strlen(tiny) < sizeof(tiny));
}

TEST(preset_model, missing_functions_and_actuators) {
  CHECK_EQ(missingFunctions(FN_DRAIN | FN_WATER_FILL, FN_WATER_FILL), FN_DRAIN);
  CHECK_EQ(missingFunctions(0, 0), 0);
  CHECK_EQ(missingFunctions(FN_NUTRIENTS, FN_ALL), 0);
  CHECK_EQ(actuatorFunction("NutrientPumpA"), FN_NUTRIENTS);
  CHECK_EQ(actuatorFunction("NutrientPumpC"), FN_NUTRIENTS);
  CHECK_EQ(actuatorFunction("WaterValve"), FN_WATER_FILL);
  CHECK_EQ(actuatorFunction("WastewaterPump"), FN_DRAIN);
  CHECK_EQ(actuatorFunction("AirPump"), FN_AIR_PUMP);
  CHECK_EQ(actuatorFunction("GrowLight"), FN_GROW_LIGHT);
  CHECK_EQ(actuatorFunction("AcidPump"), 0);  // pH is always available
  CHECK_EQ(actuatorFunction("AirPumpX"), 0);
  CHECK_EQ(actuatorFunction(nullptr), 0);
}

TEST(preset_model, volume_ranges) {
  CHECK(volumesValid(27.4f, 47.5f));
  CHECK(volumesValid(10.0f, 10.0f));
  CHECK(!volumesValid(20.0f, 10.0f));  // LOW->HIGH larger than EMPTY->HIGH
  CHECK(!volumesValid(0.5f, 10.0f));
  CHECK(!volumesValid(10.0f, 301.0f));
  CHECK(volumesValid(VOLUME_MIN_L, VOLUME_MAX_L));
}

TEST(preset_model, record_validation) {
  PresetValues v{};
  PresetRecord good = toRecord(PresetValues{10.0f, 15.0f, FN_DRAIN});
  CHECK(fromRecord(good, &v));
  CHECK_NEAR(v.delta_l, 10.0f, 1e-6);
  CHECK_EQ(v.mask, FN_DRAIN);
  PresetRecord bad = good;
  bad.version = 2;
  CHECK(!fromRecord(bad, &v));
  bad = good;
  bad.mask = 0x20;
  CHECK(!fromRecord(bad, &v));
  bad = good;
  bad.delta_l = 0.0f;
  CHECK(!fromRecord(bad, &v));
  bad = good;
  bad.total_l = NAN;
  CHECK(!fromRecord(bad, &v));
  CHECK_EQ(activeFromRecord(ActiveRecord{RECORD_VERSION, MUTTER, 0}, HAUPT), MUTTER);
  CHECK_EQ(activeFromRecord(ActiveRecord{RECORD_VERSION, 3, 0}, HAUPT), HAUPT);
  CHECK_EQ(activeFromRecord(ActiveRecord{0, MUTTER, 0}, HAUPT), HAUPT);
}

TEST(preset_model, status_text) {
  char buf[96];
  formatPresetStatus(buf, sizeof(buf), "Haupt", 69, 120, "Bloom", 6.0f, 6.4f, 2200.0f);
  CHECK_EQ(std::string(buf), std::string("Haupt \xC2\xB7 Day 69/120 \xC2\xB7 Bloom (pH 6.0-6.4, EC 2.20)"));
  formatPresetStatus(buf, sizeof(buf), "Mutter", 400, 0, "Mutter", 5.8f, 6.2f, 0.0f);
  CHECK_EQ(std::string(buf), std::string("Mutter \xC2\xB7 Day 400 \xC2\xB7 Mutter (pH 5.8-6.2, EC off)"));
  formatPresetStatus(buf, sizeof(buf), "Anzucht", 3, 21, "", 5.8f, 6.2f, 500.0f);
  CHECK_EQ(std::string(buf), std::string("Anzucht \xC2\xB7 Day 3/21 (pH 5.8-6.2, EC 0.50)"));
  formatGrowDay(buf, sizeof(buf), 69, 120);
  CHECK_EQ(std::string(buf), std::string("Day 69 / 120"));
  formatGrowDay(buf, sizeof(buf), 700, 0);
  CHECK_EQ(std::string(buf), std::string("Day 700"));
}

// ---------------------------------------------------------------------------
// GrowProfile (NVS store)
// ---------------------------------------------------------------------------

TEST(grow_profile, first_boot_is_haupt_with_yaml_defaults_and_writes_nothing) {
  GrowProfile p;
  p.set_defaults(HAUPT, 27.4f, 47.5f, FN_ALL);
  p.set_defaults(MUTTER, 12.0f, 18.0f, FN_NUTRIENTS);
  p.load();
  CHECK_EQ(p.active(), HAUPT);
  CHECK_EQ(p.current().mask, FN_ALL);
  CHECK_NEAR(p.current().delta_l, 27.4f, 1e-6);
  CHECK_NEAR(p.values(MUTTER).delta_l, 12.0f, 1e-6);
  CHECK_EQ(host::nvs_save_count, 0);
  CHECK(!p.is_stored(HAUPT));
}

TEST(grow_profile, active_index_and_edits_survive_a_reboot) {
  {
    GrowProfile p;
    p.load();
    CHECK(p.set_active(MUTTER));
    CHECK(p.set_current(PresetValues{11.0f, 16.0f, FN_NUTRIENTS | FN_DRAIN}));
  }
  GrowProfile q;  // "reboot": new object, same NVS
  q.load();
  CHECK_EQ(q.active(), MUTTER);
  CHECK_NEAR(q.current().delta_l, 11.0f, 1e-6);
  CHECK_NEAR(q.current().total_l, 16.0f, 1e-6);
  CHECK_EQ(q.current().mask, FN_NUTRIENTS | FN_DRAIN);
  CHECK(q.is_stored(MUTTER));
  // inactive presets untouched
  CHECK(!q.is_stored(HAUPT));
  CHECK_NEAR(q.values(HAUPT).delta_l, 27.4f, 1e-6);
  CHECK_EQ(q.values(ANZUCHT).mask, 0);
}

TEST(grow_profile, writes_only_on_change) {
  GrowProfile p;
  p.load();
  CHECK(p.set_active(HAUPT));  // already active
  CHECK_EQ(host::nvs_save_count, 0);
  CHECK(p.set_current(PresetValues{27.399999f, 47.5f, FN_ALL}));  // same as default (float noise)
  CHECK_EQ(host::nvs_save_count, 0);
  CHECK(p.set_current(PresetValues{27.0f, 47.5f, FN_ALL}));
  CHECK_EQ(host::nvs_save_count, 1);
  CHECK(p.set_current(PresetValues{27.0f, 47.5f, FN_ALL}));
  CHECK_EQ(host::nvs_save_count, 1);
  CHECK(p.set_active(ANZUCHT));
  CHECK_EQ(host::nvs_save_count, 2);
}

TEST(grow_profile, rejects_invalid_values) {
  GrowProfile p;
  p.load();
  CHECK(!p.set_current(PresetValues{50.0f, 40.0f, FN_ALL}));
  CHECK(!p.set_current(PresetValues{10.0f, 40.0f, 0x40}));
  CHECK(!p.set_active(3));
  CHECK_EQ(p.active(), HAUPT);
  CHECK_NEAR(p.current().delta_l, 27.4f, 1e-6);
  CHECK_EQ(host::nvs_save_count, 0);
}

TEST(grow_profile, corrupt_records_fall_back_to_defaults) {
  {
    GrowProfile p;
    p.load();
    p.set_active(MUTTER);
    p.set_current(PresetValues{11.0f, 16.0f, FN_NUTRIENTS});
  }
  // corrupt both records (wrong version / out-of-range index)
  for (auto &kv : host::nvs) {
    if (kv.second.size() == sizeof(PresetRecord)) kv.second[8] = 9;
    if (kv.second.size() == sizeof(ActiveRecord)) kv.second[2] = 7;
  }
  GrowProfile q;
  q.load();
  CHECK_EQ(q.active(), HAUPT);
  CHECK_NEAR(q.values(MUTTER).delta_l, 10.0f, 1e-6);
  CHECK(!q.is_stored(MUTTER));
}
