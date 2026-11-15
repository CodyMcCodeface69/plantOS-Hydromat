// Alert bookkeeping of CentralStatusLogger: what raises, updates, resolves and clears an alert,
// and the text the "Alert State" web UI sensor shows (getAlertStateShort()).
// Real code under test: components/plantos_controller/CentralStatusLogger.*
// Written against the code of 2026-11-15 before the report rewrite, to pin the semantics.
#include <string>
#include "minitest.h"
#include "esphome/core/hal.h"
#include "esphome/components/plantos_controller/CentralStatusLogger.h"

namespace {
size_t active_count(const CentralStatusLogger &l) { return l.getActiveAlerts().size(); }
}  // namespace

TEST(alerts, empty_logger_is_clear) {
  CentralStatusLogger l;
  CHECK(!l.hasActiveAlerts());
  CHECK_EQ(l.getAlertStateShort(), std::string("Clear"));
}

TEST(alerts, update_adds_then_refreshes_reason_and_age) {
  CentralStatusLogger l;
  host::set_millis(1000);
  l.updateAlertStatus("EC_HIGH", "first");
  host::set_millis(5000);
  l.updateAlertStatus("EC_HIGH", "second");
  auto a = l.getActiveAlerts();
  REQUIRE(a.size() == 1);
  CHECK_EQ(a[0].reason, std::string("second"));
  CHECK_EQ(a[0].timestamp, (uint32_t)5000);  // age restarts on every update
  CHECK_EQ(l.getAlertStateShort(), std::string("EC HIGH"));
}

TEST(alerts, update_with_empty_reason_clears) {
  CentralStatusLogger l;
  l.updateAlertStatus("TEMP_HIGH", "29 C");
  l.updateAlertStatus("TEMP_HIGH", "");
  CHECK(!l.hasActiveAlerts());
  CHECK_EQ(l.getAlertStateShort(), std::string("Clear"));
}

TEST(alerts, clear_removes_active_and_resolved) {
  CentralStatusLogger l;
  l.updateAlertStatus("SHELLY_OFFLINE", "no answer");
  l.clearAlert("SHELLY_OFFLINE");
  CHECK(!l.hasActiveAlerts());
  l.updateAlertStatus("EC_LOW", "x");
  l.resolveAlert("EC_LOW");
  l.clearAlert("EC_LOW");
  CHECK(!l.hasActiveAlerts());  // the resolved entry is gone too
  l.clearAlert("NOT_THERE");    // no-op
}

TEST(alerts, resolve_hides_from_alert_state) {
  CentralStatusLogger l;
  host::set_millis(1000);
  l.updateAlertStatus("PH_CRITICAL", "pH 4.8");
  host::set_millis(3000);
  l.resolveAlert("PH_CRITICAL");
  CHECK_EQ(active_count(l), (size_t)0);
  CHECK_EQ(l.getAlertStateShort(), std::string("Clear"));
  // SUSPECT: hasActiveAlerts() counts resolved entries too (the list keeps them)
  CHECK(l.hasActiveAlerts());
  l.resolveAlert("PH_CRITICAL");  // already resolved: no-op
  l.resolveAlert("NOT_THERE");
}

TEST(alerts, update_after_resolve_stays_resolved) {
  // SUSPECT: updateAlertStatus() finds the RESOLVED entry of the same type and only changes its
  // reason and timestamp, so the alert never becomes active again (until clearAlert or reboot).
  // Affects types raised with updateAlertStatus and ended with resolveAlert: EC_HIGH, EC_LOW,
  // PH_CRITICAL. Kept as is on purpose (owner decision); see plan/LOG.md.
  CentralStatusLogger l;
  l.updateAlertStatus("EC_HIGH", "first");
  l.resolveAlert("EC_HIGH");
  l.updateAlertStatus("EC_HIGH", "again");
  CHECK_EQ(active_count(l), (size_t)0);
  CHECK_EQ(l.getAlertStateShort(), std::string("Clear"));
}

TEST(alerts, context_alert_new_update_and_after_resolve) {
  CentralStatusLogger l;
  host::set_millis(1000);
  l.updateAlertWithContext("PUMP_REJECTION_ACID", "rejected", "root", "check pump", "ctx", "plan", 3);
  auto a = l.getActiveAlerts();
  REQUIRE(a.size() == 1);
  CHECK_EQ(a[0].reason, std::string("rejected"));
  CHECK_EQ(a[0].user_action, std::string("check pump"));
  host::set_millis(4000);
  l.updateAlertWithContext("PUMP_REJECTION_ACID", "rejected 2", "root", "check pump 2", "ctx", "plan", 3);
  a = l.getActiveAlerts();
  REQUIRE(a.size() == 1);
  CHECK_EQ(a[0].reason, std::string("rejected 2"));
  CHECK_EQ(a[0].user_action, std::string("check pump 2"));
  CHECK_EQ(a[0].timestamp, (uint32_t)1000);  // update keeps the age
  // After a resolve, the same type raises a NEW active entry (the resolved one stays)
  l.resolveAlert("PUMP_REJECTION_ACID");
  CHECK_EQ(active_count(l), (size_t)0);
  l.updateAlertWithContext("PUMP_REJECTION_ACID", "rejected 3", "", "", "", "", 0);
  a = l.getActiveAlerts();
  REQUIRE(a.size() == 1);
  CHECK_EQ(a[0].reason, std::string("rejected 3"));
  CHECK_EQ(a[0].timestamp, (uint32_t)4000);
  // The next resolve hits the active one; a clear removes both
  l.resolveAlert("PUMP_REJECTION_ACID");
  CHECK_EQ(active_count(l), (size_t)0);
  l.clearAlert("PUMP_REJECTION_ACID");
  CHECK(!l.hasActiveAlerts());
}

TEST(alerts, context_alert_after_plain_alert_of_same_type) {
  // A plain alert (updateAlertStatus) and a context alert share the entry while it is active
  CentralStatusLogger l;
  l.updateAlertStatus("PH_SENSOR_CRITICAL", "plain");
  l.updateAlertWithContext("PH_SENSOR_CRITICAL", "ctx", "", "act", "", "", 0);
  auto a = l.getActiveAlerts();
  REQUIRE(a.size() == 1);
  CHECK_EQ(a[0].reason, std::string("ctx"));
  CHECK_EQ(a[0].user_action, std::string("act"));
}

TEST(alerts, alert_state_texts) {
  struct Case {
    const char *type, *text;
  } cases[] = {
      {"PH_CRITICAL", "pH Critical"},
      {"PH_SENSOR_CRITICAL", "pH Sensor Fail"},
      {"PH_SENSOR_HARDWARE_FAILURE", "pH Hardware Fail"},
      {"NO_PH_READINGS", "No pH Readings"},
      {"PH_RANGE", "pH Out of Range"},
      {"CALIBRATION_FAILED_MID", "Calibration Fail"},
      {"PUMP_REJECTION_ACID", "Pump Reject: Acid"},
      {"PUMP_REJECTION_WATER_VALVE", "Pump Reject: Water"},
      {"PUMP_REJECTION_WASTEWATER", "Pump Reject: Waste"},
      {"PUMP_REJECTION_NUTRIENT_A", "Pump Rejected"},
      {"PUMP_FAILURE", "Pump Failure"},
      {"SAFETY_GATE_REJECT_X", "Safety Reject"},
      {"DURATION_ADAPTED_AcidPump", "Duration Adapted"},
      {"HARDWARE_WATER_SENSOR_HIGH", "Water Sensor: High"},
      {"HARDWARE_WATER_SENSOR_EMPTY", "Water Sensor: Empty"},
      {"HARDWARE_WATER_SENSORS_MISSING", "Water Sensors Missing"},
      {"WATER_LOW", "Water Low"},
      {"HARDWARE_HAL_MISSING", "HAL Missing"},
      {"SHELLY_OFFLINE", "Shelly Offline"},
      {"TEMPERATURE", "Temp High"},
      {"TEMP_HIGH", "Temp High"},
      {"SPILL", "Spill Detected"},
      {"PSM_TEST_OK", "PSM Test OK"},
      {"PSM_TEST_WARNING", "PSM Warning"},
      {"PSM_RECOVERY", "PSM Recovery"},
      {"PSM_TEST_UNPLUG", "PSM Test Active"},
      {"EC_CALIBRATION_DUE", "EC CALIBRATION DUE"},
      {"TEMP_LOW", "TEMP LOW"},
  };
  for (auto &c : cases) {
    CentralStatusLogger l;
    l.updateAlertStatus(c.type, "r");
    if (l.getAlertStateShort() != c.text) fprintf(stderr, "    case %s\n", c.type);
    CHECK_EQ(l.getAlertStateShort(), std::string(c.text));
  }
}

TEST(alerts, alert_state_with_several_alerts_names_the_oldest_entry) {
  CentralStatusLogger l;
  l.updateAlertStatus("SHELLY_OFFLINE", "a");
  l.updateAlertStatus("TEMP_HIGH", "b");
  l.updateAlertStatus("EC_LOW", "c");
  CHECK_EQ(l.getAlertStateShort(), std::string("Shelly Offline (+2 more)"));
  l.resolveAlert("SHELLY_OFFLINE");
  CHECK_EQ(l.getAlertStateShort(), std::string("Temp High (+1 more)"));
  l.updateAlertStatus("TEMP_HIGH", "b2");  // update keeps the position
  CHECK_EQ(l.getAlertStateShort(), std::string("Temp High (+1 more)"));
}
