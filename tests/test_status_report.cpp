// Status report (status_report.cpp): one line per topic, values read at print time.
// PLANTOS_TEST_VERBOSE=1 make run ARGS=status_report prints a sample report.
#include <cstdlib>
#include <string>
#include <vector>
#include "minitest.h"
#include "fixture.h"

using plantos_controller::ControllerState;

namespace {

struct Report {
  std::vector<std::string> lines;
  bool has(const std::string &text) const {
    for (auto &l : lines)
      if (l.find(text) != std::string::npos) return true;
    return false;
  }
};

Report print(ControllerFixture &f) {
  Report r;
  host::log_sink = &r.lines;
  f.ctrl.printStatusReport();
  host::log_sink = nullptr;
  if (getenv("PLANTOS_TEST_VERBOSE"))
    for (auto &l : r.lines) fprintf(stderr, "    %s\n", l.c_str());
  return r;
}

}  // namespace

TEST(status_report, contains_every_topic) {
  ControllerFixture f;
  f.set_time(2026, 11, 15, 14, 3, 12);
  host::set_millis(3600000);
  f.force_state(ControllerState::IDLE);
  host::advance_millis(42000);
  f.ctrl.onWifiConnected("192.168.0.201");
  f.hal.ph = 6.12f;
  f.hal.ec = 1450.0f;
  f.hal.temperature = 21.3f;
  f.hal.has_temperature = true;
  f.hal.level_low = true;
  f.hal.level_empty = true;
  f.hal.pump_states["AirPump"] = true;
  f.hal.pump_states["GrowLight"] = true;
  f.ctrl.ph_K_ = 0.07f;
  f.ctrl.ec_K_feed_ = 0.00015f;
  f.ctrl.status_logger_.updateI2CHardwareStatus({{0x76, "BME280 Air Sensor", true, true}});
  f.ctrl.status_logger_.updateAlertStatus("TEMP_HIGH", "Water 29.1 C");
  f.ctrl.status_logger_.updateAlertWithContext("PUMP_REJECTION_ACID", "Acid pump rejected", "", "Check max duration",
                                              "", "", 0);
  f.ctrl.recordRefusal("feed", "night mode");
  f.psm.logEvent("PH_CORRECTION", 0);
  host::advance_millis(5000);

  Report r = print(f);
  CHECK(r.has("=== PlantOS status 2026-11-15 "));  // hour from the host's localtime()
  CHECK(r.has(":03:12 | up 01:00:47 | heap "));
  CHECK(r.has("IP 192.168.0.201"));
  CHECK(r.has("State: IDLE for 47 s | Haupt"));
  CHECK(r.has("Calendar: pH "));
  CHECK(r.has("Sensors: pH 6.12 | EC 1450 uS/cm | water 21.3 C | level H/L/E 0/1/1 NORMAL | air -"));
  CHECK(r.has("Actuators: acid 0 A 0 B 0 C 0 valve 0 | air 1 drain 0 light 1"));
  CHECK(r.has("Hardware: EZO pH ok | DS18B20 ok | level sensors ok | I2C 0x76 BME280 Air Sensor ok | Shelly online, up 0h16m"));
  CHECK(r.has("K: pH 0.0700 | EC feed 0.000150 | EC calibration 1.000"));
  CHECK(r.has("PSM: PH_CORRECTION (status 0,"));
  CHECK(r.has("Last refusal: ") && r.has("feed: night mode"));
  CHECK(r.has("[W][plantos_controller] Alerts (2):"));
  CHECK(r.has("[1] TEMP_HIGH: Water 29.1 C (5 s)"));
  CHECK(r.has("[2] PUMP_REJECTION_ACID: Acid pump rejected (5 s) - Check max duration"));
  CHECK(r.lines.size() <= 14);
}

TEST(status_report, missing_values_show_a_dash) {
  ControllerFixture f(false);  // no calendar
  f.hal.has_ph = false;
  f.hal.has_ec = false;
  f.hal.has_level_sensors = false;
  f.hal.shelly_reachable = false;
  Report r = print(f);
  CHECK(r.has("=== PlantOS status time not synced |"));
  CHECK(r.has("Calendar: not configured"));
  CHECK(r.has("Sensors: pH - | EC - | water - | level - | air -"));
  CHECK(r.has("level sensors MISSING | I2C not scanned | Shelly OFFLINE"));
  CHECK(r.has("PSM: -"));
  CHECK(r.has("Last refusal: None"));
  CHECK(r.has("Alerts: none"));
}

TEST(status_report, level_names) {
  struct Case {
    bool h, l, e;
    const char *text;
  } cases[] = {{1, 1, 1, "FULL"}, {0, 1, 1, "NORMAL"}, {0, 0, 1, "LOW (auto-feed zone)"},
               {0, 0, 0, "EMPTY (below EMPTY sensor)"}, {1, 0, 1, "ERROR (HIGH without LOW"}};
  for (auto &c : cases) {
    ControllerFixture f;
    f.hal.level_high = c.h;
    f.hal.level_low = c.l;
    f.hal.level_empty = c.e;
    CHECK(print(f).has(c.text));
  }
}

TEST(status_report, on_request_and_periodic) {
  ControllerFixture f;
  std::vector<std::string> lines;
  host::log_sink = &lines;
  host::set_millis(1000);
  f.ctrl.updateStatusReport();
  CHECK(lines.empty());  // periodic off at boot
  f.ctrl.requestStatusReport();
  f.ctrl.updateStatusReport();
  size_t one = lines.size();
  CHECK(one > 5);
  f.ctrl.updateStatusReport();
  CHECK_EQ(lines.size(), one);  // once per request
  f.ctrl.setPeriodicStatusReport(true);
  size_t before = lines.size();
  host::advance_millis(59999);
  f.ctrl.updateStatusReport();
  CHECK_EQ(lines.size(), before);
  host::advance_millis(1);
  f.ctrl.updateStatusReport();
  CHECK(lines.size() > before);
  f.ctrl.setPeriodicStatusReport(false);
  before = lines.size();
  host::advance_millis(120000);
  f.ctrl.updateStatusReport();
  CHECK_EQ(lines.size(), before);
  host::log_sink = nullptr;
}
