// FakeHAL: a scriptable implementation of the abstract plantos_hal::HAL interface.
// Sensor values and tank geometry are plain public fields; actuator calls are recorded.
#pragma once
#include <string>
#include <vector>
#include "esphome/core/hal.h"
#include "esphome/components/plantos_hal/hal.h"

struct FakeHAL : public plantos_hal::HAL {
  // --- scriptable inputs (defaults = production values from plantOS.yaml where it matters) ---
  float tank_volume_delta = 27.4f;
  float total_tank_volume = 47.5f;
  float mag_valve_flow = 100.0f;
  std::map<std::string, plantos_hal::PumpConfig> pump_configs{
      {"AcidPump", {"AcidPump", 2.35f, 1.0f}},
      {"NutrientPumpA", {"NutrientPumpA", 1.75f, 1.0f}},
      {"NutrientPumpB", {"NutrientPumpB", 0.75f, 1.0f}},
      {"NutrientPumpC", {"NutrientPumpC", 0.75f, 1.0f}},
  };
  float ph = 6.0f;
  bool has_ph = true;
  float ec = 1000.0f;  // uS/cm
  bool has_ec = true;
  float temperature = 21.0f;
  bool has_temperature = false;
  bool level_high = false, level_low = false, level_empty = false;
  bool has_level_sensors = true;
  // Time as ESPHomeHAL reports it: UTC unix timestamp + LOCAL seconds since midnight.
  // ControllerFixture::set_time() fills all three consistently.
  int64_t timestamp = 0;  // 0 = no time
  uint32_t seconds_since_midnight = 0;
  bool has_time = false;
  uint32_t ph_interval_ms = 7200000;
  float ph_min = 5.5f, ph_max = 6.2f;
  bool shelly_enabled = true, shelly_reachable = true;
  bool ph_sensor_ready = true;
  int ph_calib_fail_point = -1;  // startPhCalibration() fails for this point (0 mid, 1 low, 2 high)

  // --- recorded outputs ---
  struct Call {
    std::string kind;  // "pump" | "valve" | "pattern" | "stop_sequence"
    std::string id;
    bool state;
    uint32_t at_ms;  // fake millis() when the call happened
  };
  std::vector<Call> calls;
  std::map<std::string, bool> pump_states, valve_states;
  std::vector<uint32_t> last_pattern;
  struct CalibCommand {
    float ph;
    int point;
    uint32_t at_ms;
  };
  std::vector<CalibCommand> calib_commands;
  int single_readings = 0;
  int temp_compensations = 0;

  int count(const std::string &kind, const std::string &id, bool state) const {
    int n = 0;
    for (auto &c : calls)
      if (c.kind == kind && c.id == id && c.state == state) n++;
    return n;
  }

  // --- HAL interface ---
  void setPump(const std::string &id, bool state) override {
    calls.push_back({"pump", id, state, esphome::millis()});
    pump_states[id] = state;
  }
  void setPump(const std::string &id, bool state, float) override { setPump(id, state); }
  float pumpflow(const std::string &id, float targetML) override {
    // Same contract as ESPHomeHAL::pumpflow: seconds = mL / flow rate.
    // (unknown pump: 1 mL/s; flow rate <= 0: 0 s)
    auto it = pump_configs.find(id);
    if (it == pump_configs.end()) return targetML / 1.0f;
    if (it->second.flow_rate_ml_s <= 0.0f) return 0.0f;
    return targetML / it->second.flow_rate_ml_s;
  }
  plantos_hal::PumpConfig getPumpConfig(const std::string &id) const override {
    auto it = pump_configs.find(id);
    return it == pump_configs.end() ? plantos_hal::PumpConfig() : it->second;
  }
  void setPumpConfig(const std::string &id, float flow, float pwm) override {
    pump_configs[id] = plantos_hal::PumpConfig(id, flow, pwm);
  }
  void setPumpPwm(const std::string &id, float pwm) override { pump_configs[id].pwm_intensity = pwm; }
  void setTankVolumeDelta(float v) override { tank_volume_delta = v; }
  float getTankVolumeDelta() const override { return tank_volume_delta; }
  void setTotalTankVolume(float v) override { total_tank_volume = v; }
  float getTotalTankVolume() const override { return total_tank_volume; }
  void setMagValveFlowRate(float v) override { mag_valve_flow = v; }
  float getMagValveFlowRate() const override { return mag_valve_flow; }
  float valveflow(float targetML) override { return mag_valve_flow > 0 ? targetML / mag_valve_flow : 0.0f; }
  void setValve(const std::string &id, bool state) override {
    calls.push_back({"valve", id, state, esphome::millis()});
    valve_states[id] = state;
  }
  bool getPumpState(const std::string &id) const override {
    auto it = pump_states.find(id);
    return it != pump_states.end() && it->second;
  }
  bool getValveState(const std::string &id) const override {
    auto it = valve_states.find(id);
    return it != valve_states.end() && it->second;
  }
  bool checkShellySwitchStatus(const std::string &) override { return true; }
  bool setAirPumpPattern(const std::vector<uint32_t> &pattern, bool finalState) override {
    calls.push_back({"pattern", "AirPump", finalState, esphome::millis()});
    last_pattern = pattern;
    return true;
  }
  bool stopAirPumpSequence(bool finalState) override {
    calls.push_back({"stop_sequence", "AirPump", finalState, esphome::millis()});
    return true;
  }
  bool isSequenceRunning(uint8_t) const override { return false; }
  void ensureNoSequenceRunning(uint8_t, bool) override {}
  void setShellySwitch(uint8_t, bool) override {}
  float readPH() override { return ph; }
  bool hasPhValue() const override { return has_ph; }
  void onPhChange(std::function<void(float)>) override {}
  bool startPhCalibration(float ph_value, int point) override {
    calib_commands.push_back({ph_value, point, esphome::millis()});
    return point != ph_calib_fail_point;
  }
  bool isPhSensorReady() const override { return ph_sensor_ready; }
  void setPhVerbose(bool) override {}
  void queryPhCalibrationStatus() override {}
  bool takeSinglePhReading(float &value) override {
    single_readings++;
    value = ph;
    return has_ph;
  }
  float getLastPhReading() override { return ph; }
  void requestPhReading() override {}
  uint32_t get_ph_reading_interval() const override { return ph_interval_ms; }
  float get_ph_min() const override { return ph_min; }
  float get_ph_max() const override { return ph_max; }
  float readWaterLevel() override { return 0.0f; }
  bool hasWaterLevel() const override { return false; }
  bool readWaterLevelHigh() override { return level_high; }
  bool readWaterLevelLow() override { return level_low; }
  bool readWaterLevelEmpty() override { return level_empty; }
  bool hasWaterLevelSensors() const override { return has_level_sensors; }
  float readLightIntensity() override { return 0.0f; }
  bool hasLightIntensity() const override { return false; }
  float readTemperature() override { return temperature; }
  bool hasTemperature() const override { return has_temperature; }
  void onTemperatureChange(std::function<void(float)>) override {}
  float readEC() override { return ec; }
  bool hasECValue() const override { return has_ec; }
  void onECChange(std::function<void(float)>) override {}
  bool setECCalibrationFactor(float) override { return true; }
  float getECCalibrationFactor() const override { return 1.0f; }
  bool resetECCalibrationFactor() override { return true; }
  bool sendPhTemperatureCompensation(float) override {
    temp_compensations++;
    return true;
  }
  void setSystemLED(float, float, float, float = 1.0f) override {}
  void turnOffLED() override {}
  bool isLEDOn() const override { return false; }
  uint32_t getSystemTime() const override { return esphome::millis(); }
  int64_t getCurrentTimestamp() const override { return timestamp; }
  uint32_t getSecondsSinceMidnight() const override { return seconds_since_midnight; }
  bool hasTime() const override { return has_time; }
  void setShellyEnabled(bool e) override { shelly_enabled = e; }
  bool isShellyEnabled() const override { return shelly_enabled; }
  bool isShellyReachable() const override { return shelly_reachable; }
  uint32_t getShellyUptime() const override { return 1000; }
  void updateShellyHealth(bool, uint32_t = 0) override {}
  void updateShellySwitchState(uint8_t, bool) override {}
  bool getShellySwitchState(uint8_t) const override { return false; }
  uint32_t getLastShellyStateUpdate() const override { return 0; }
};
