// EZO pH UART driver (components/ezo_ph_uart) against a fake EZO circuit on a fake UART
// (stubs/esphome/components/uart/uart.h). Pins the response parser, the command strings and
// the periodic read sequence (T,<temp> -> R) with its timing, and that the periodic path never
// blocks the main loop. Time inside a call only moves through delay(), so "blocked" = how far
// millis() moved during one loop() call.
#include <algorithm>
#include <cstdio>
#include <string>
#include "minitest.h"
#include "esphome/core/hal.h"
#include "esphome/components/ezo_ph_uart/ezo_ph_uart.h"

using esphome::ezo_ph_uart::EZOPHUARTComponent;
using esphome::sensor::Sensor;

namespace {

// The circuit: RESPONSE,1 (factory default, "*OK" after every command) or RESPONSE,0 (no
// "*OK"; a T command then has no answer at all). Reading "R" takes 900 ms (datasheet).
struct FakeEzo {
  bool ok_codes = true;
  bool echo = false;    // circuit echoes the command letter first
  bool dead = false;    // probe / circuit unplugged: no bytes at all
  std::string reading = "6.54";
  uint32_t r_ms = 900, cmd_ms = 300;
  std::vector<std::string> temps;  // T values received

  void attach(EZOPHUARTComponent &c) {
    c.responder = [this, &c](const std::string &cmd) {
      if (dead) return;
      uint32_t now = esphome::millis();
      if (echo) c.host_rx(now + 5, cmd.substr(0, 1) + "\r");
      std::string ok = ok_codes ? "*OK\r" : "";
      if (cmd == "R") {
        c.host_rx(now + r_ms, reading + "\r" + ok);
      } else if (cmd.rfind("T,", 0) == 0) {
        temps.push_back(cmd.substr(2));
        if (ok_codes) c.host_rx(now + cmd_ms, ok);
      } else if (cmd == "Cal,?") {
        c.host_rx(now + cmd_ms, "?Cal,3\r" + ok);
      } else {
        if (ok_codes) c.host_rx(now + cmd_ms, ok);  // Cal,mid,..., C,1, C,0, L,1 ...
      }
    };
  }
};

struct Rig {
  EZOPHUARTComponent c;
  Sensor ph, temp;
  FakeEzo ezo;
  uint32_t max_block = 0;  // longest single loop() call (fake ms)
  uint32_t next_update_ = 0;

  explicit Rig(bool with_temp = true) {
    c.set_ph_sensor(&ph);
    if (with_temp) {
      temp.set_test_state(21.34f);
      c.set_temperature_compensation(&temp);
    }
    ezo.attach(c);
    c.setup();
  }
  // Main loop: loop() every 10 ms of fake time up to `until_ms`, update() every 5 s from `first_update`
  void run(uint32_t until_ms, int64_t first_update = -1) {
    if (first_update >= 0 && (int64_t) next_update_ < first_update) next_update_ = first_update;
    while (esphome::millis() < until_ms) {
      uint32_t now = esphome::millis();
      if (first_update >= 0 && now >= next_update_) {  // scheduler: due time, even after a blocked loop
        c.update();
        next_update_ += 5000;
      }
      uint32_t before = esphome::millis();
      c.loop();
      max_block = std::max(max_block, esphome::millis() - before);
      host::advance_millis(10);
    }
  }
  std::vector<std::string> cmds() const {
    std::vector<std::string> v;
    for (auto &l : c.tx_lines) v.push_back(l.cmd);
    return v;
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

TEST(ezo_ph_uart, parse_ph_value) {
  EZOPHUARTComponent c;
  float v = 0;
  CHECK(c.parse_ph_value_("6.54", v));
  CHECK_NEAR(v, 6.54f, 1e-5);
  CHECK(c.parse_ph_value_("*OK,7.01", v));
  CHECK_NEAR(v, 7.01f, 1e-5);
  CHECK(c.parse_ph_value_("12", v));
  CHECK_NEAR(v, 12.0f, 1e-5);
  CHECK(c.parse_ph_value_("-0.5", v));  // range check is the caller's job
  for (const char *bad : {"", "*OK", "*ER", "*OV", "*UV", "C,1", "C,0", "T,21.3", "?Cal,3", "Cal,mid,7.00", "abc"}) {
    if (c.parse_ph_value_(bad, v)) fprintf(stderr, "    accepted '%s'\n", bad);
    CHECK(!c.parse_ph_value_(bad, v));
  }
  CHECK(!c.parse_ph_value_(nullptr, v));
  // SUSPECT: "*RS" (reset) is not in the error list and parses as a failed float -> false anyway
  CHECK(!c.parse_ph_value_("*RS", v));
}

TEST(ezo_ph_uart, check_response_code) {
  EZOPHUARTComponent c;
  for (const char *ok : {"*OK", "*OK,1", "6.54", "C,0", "?Cal,2"}) CHECK(c.check_response_code_(ok));
  for (const char *bad : {"", "*ER", "*OV", "*UV", "*RS"}) CHECK(!c.check_response_code_(bad));
  CHECK(!c.check_response_code_(nullptr));
}

TEST(ezo_ph_uart, read_response_lines) {
  host::set_millis(1000);
  EZOPHUARTComponent c;
  char buf[32];
  c.host_rx(1000, "\r\n6.54 \t\r*OK\r");
  CHECK(c.read_response_(buf, sizeof(buf)));
  CHECK_EQ(std::string(buf), std::string("6.54"));  // leading CR/LF skipped, trailing blanks cut
  CHECK(c.read_response_(buf, sizeof(buf)));
  CHECK_EQ(std::string(buf), std::string("*OK"));
  // Command echo: a single R/C/T/i is dropped and the next line read
  c.host_rx(esphome::millis(), "R\r7.10\r");
  CHECK(c.read_response_(buf, sizeof(buf)));
  CHECK_EQ(std::string(buf), std::string("7.10"));
  // 31 characters without CR fill the buffer and count as a line
  c.host_rx(esphome::millis(), std::string(40, '7'));
  CHECK(c.read_response_(buf, sizeof(buf)));
  CHECK_EQ(std::string(buf).size(), (size_t) 31);
}

TEST(ezo_ph_uart, read_response_timeout) {
  host::set_millis(1000);
  EZOPHUARTComponent c;
  char buf[32];
  CHECK(!c.read_response_(buf, sizeof(buf)));  // nothing within 1 s
  CHECK_EQ(esphome::millis(), (uint32_t) 2000);
  c.host_rx(2100, "6.5");  // partial line, then silence: returned as it is
  CHECK(c.read_response_(buf, sizeof(buf)));
  CHECK_EQ(std::string(buf), std::string("6.5"));
}

// ---------------------------------------------------------------------------
// Periodic read: update() every 5 s, loop() drives the rest
// ---------------------------------------------------------------------------

TEST(ezo_ph_uart, periodic_read_with_ok_codes) {
  host::set_millis(10000);
  Rig r;
  r.run(10000 + 5000, 10000);  // one update at t=10000
  auto &tx = r.c.tx_lines;
  REQUIRE(tx.size() == 2);
  CHECK_EQ(tx[0].cmd, std::string("T,21.3"));  // temperature compensation, one decimal
  CHECK_EQ(tx[0].at_ms, (uint32_t) 10000);
  CHECK_EQ(tx[1].cmd, std::string("R"));
  // T answered "*OK" at +300; R goes out on the first loop() at or after +300
  CHECK(tx[1].at_ms >= 10300 && tx[1].at_ms <= 10320);
  REQUIRE(r.ph.published.size() == 1);
  CHECK_NEAR(r.ph.published[0], 6.54f, 1e-5);
  CHECK_EQ(r.c.error_count_, 0);
  CHECK(r.c.reading_state_ == EZOPHUARTComponent::ReadingState::IDLE);
  CHECK_EQ(r.ezo.temps.size(), (size_t) 1);
}

TEST(ezo_ph_uart, periodic_read_without_ok_codes_waits_1_s_for_t) {
  // RESPONSE,0: T has no answer. The driver waits 300 ms + the 1 s read timeout, then sends R.
  host::set_millis(10000);
  Rig r;
  r.ezo.ok_codes = false;
  r.run(10000 + 5000, 10000);
  auto &tx = r.c.tx_lines;
  REQUIRE(tx.size() == 2);
  CHECK_EQ(tx[1].cmd, std::string("R"));
  CHECK(tx[1].at_ms >= 11300 && tx[1].at_ms <= 11320);
  REQUIRE(r.ph.published.size() == 1);
  CHECK_NEAR(r.ph.published[0], 6.54f, 1e-5);
  CHECK_EQ(r.c.error_count_, 0);  // a missing T answer is not an error
}

TEST(ezo_ph_uart, periodic_read_without_temperature_sends_r_only) {
  host::set_millis(10000);
  Rig r(false);
  r.run(15000, 10000);
  REQUIRE(r.c.tx_lines.size() == 1);
  CHECK_EQ(r.c.tx_lines[0].cmd, std::string("R"));
  CHECK_EQ(r.c.tx_lines[0].at_ms, (uint32_t) 10000);
  REQUIRE(r.ph.published.size() == 1);
}

TEST(ezo_ph_uart, periodic_read_cadence_and_order_over_a_minute) {
  // RESPONSE,0: the mode the deployed circuit evidently runs in (the driver's T handling says
  // "T does NOT return a response"; with *OK on, see the SUSPECT test below)
  host::set_millis(10000);
  Rig r;
  r.ezo.ok_codes = false;
  r.run(70000, 10000);
  auto cmds = r.cmds();
  REQUIRE(cmds.size() == 24);  // 12 updates x (T, R)
  for (size_t i = 0; i < cmds.size(); i += 2) {
    CHECK_EQ(cmds[i], std::string("T,21.3"));
    CHECK_EQ(cmds[i + 1], std::string("R"));
    CHECK_EQ(r.c.tx_lines[i].at_ms, (uint32_t) (10000 + 5000 * (i / 2)));
  }
  CHECK_EQ(r.ph.published.size(), (size_t) 12);
}

TEST(ezo_ph_uart, ok_codes_on_shift_the_lines_by_one) {
  // SUSPECT: with *OK on (RESPONSE,1, factory default) the R answer is "6.54\r*OK\r"; the driver
  // reads one line per command, so the leftover "*OK" is taken as the next T answer, T's own
  // "*OK" as the next R answer (parse error), and so on: only 4 of 12 readings per minute get
  // through and error_count_ climbs. Pinned as is; the deployed circuit runs RESPONSE,0.
  host::set_millis(10000);
  Rig r;
  r.run(70000, 10000);
  CHECK_EQ(r.cmds().size(), (size_t) 24);
  CHECK_EQ(r.ph.published.size(), (size_t) 4);
}

TEST(ezo_ph_uart, temperature_change_reaches_the_probe) {
  host::set_millis(10000);
  Rig r;
  r.run(12000, 10000);
  r.temp.set_test_state(25.06f);
  r.run(17000, 15000);
  REQUIRE(r.ezo.temps.size() == 2);
  CHECK_EQ(r.ezo.temps[0], std::string("21.3"));
  CHECK_EQ(r.ezo.temps[1], std::string("25.1"));
}

TEST(ezo_ph_uart, periodic_read_with_echo) {
  host::set_millis(10000);
  Rig r;
  r.ezo.echo = true;
  r.run(15000, 10000);
  REQUIRE(r.ph.published.size() == 1);
  CHECK_NEAR(r.ph.published[0], 6.54f, 1e-5);
}

TEST(ezo_ph_uart, update_while_reading_is_skipped) {
  host::set_millis(10000);
  Rig r;
  r.c.update();
  r.c.update();  // still waiting for T
  CHECK_EQ(r.c.tx_lines.size(), (size_t) 1);
}

TEST(ezo_ph_uart, probe_unplugged_marks_sensor_not_ready_after_6_failed_reads) {
  host::set_millis(10000);
  Rig r;
  r.ezo.dead = true;
  r.run(10000 + 5 * 5000 + 4000, 10000);  // 6 updates
  CHECK_EQ(r.c.error_count_, 6);
  CHECK(!r.c.is_sensor_ready());
  CHECK(r.ph.published.empty());
  size_t sent = r.c.tx_lines.size();
  r.run(10000 + 8 * 5000, 10000);  // not ready: update() sends nothing more
  CHECK_EQ(r.c.tx_lines.size(), sent);
}

TEST(ezo_ph_uart, bad_values_count_as_errors_good_value_resets) {
  host::set_millis(10000);
  Rig r;
  r.ezo.ok_codes = false;
  r.ezo.reading = "*ER";
  r.run(15000, 10000);
  CHECK_EQ(r.c.error_count_, 1);
  r.ezo.reading = "25.0";  // > PH_MAX 19
  r.run(20000, 15000);
  CHECK_EQ(r.c.error_count_, 2);
  CHECK(r.ph.published.empty());
  r.ezo.reading = "5.90";
  r.run(25000, 20000);
  CHECK_EQ(r.c.error_count_, 0);
  REQUIRE(r.ph.published.size() == 1);
  CHECK_NEAR(r.ph.published[0], 5.90f, 1e-5);
  CHECK_NEAR(r.c.get_last_reading(), 5.90f, 1e-5);
}

// ---------------------------------------------------------------------------
// Operator-triggered command paths (blocking by design, see the file header)
// ---------------------------------------------------------------------------

TEST(ezo_ph_uart, single_reading_for_calibration) {
  host::set_millis(10000);
  Rig r;
  float v = 0;
  CHECK(r.c.take_single_reading(v));
  CHECK_NEAR(v, 6.54f, 1e-5);
  auto cmds = r.cmds();
  REQUIRE(cmds.size() == 2);
  CHECK_EQ(cmds[0], std::string("T,21.3"));
  CHECK_EQ(cmds[1], std::string("R"));
  // T: 300 ms wait + "*OK" already there; R: 300 ms wait + 600 ms until the reading arrives
  CHECK_EQ(esphome::millis() - 10000, (uint32_t) 1200);
}

TEST(ezo_ph_uart, calibration_commands) {
  host::set_millis(10000);
  Rig r;
  CHECK(r.c.calibrate_mid(7.0f));
  CHECK(r.c.calibrate_low(4.0f));
  CHECK(r.c.calibrate_high(10.01f));
  auto cmds = r.cmds();
  REQUIRE(cmds.size() == 3);
  CHECK_EQ(cmds[0], std::string("Cal,mid,7.00"));
  CHECK_EQ(cmds[1], std::string("Cal,low,4.00"));
  CHECK_EQ(cmds[2], std::string("Cal,high,10.01"));
  CHECK_EQ(esphome::millis() - 10000, (uint32_t) 900);  // 3 x 300 ms
  r.c.query_calibration_status();
  CHECK_EQ(std::string(r.cmds().back()), std::string("Cal,?"));
}

TEST(ezo_ph_uart, temperature_compensation_command) {
  host::set_millis(10000);
  Rig r;
  CHECK(r.c.send_temperature_compensation(19.5f));
  CHECK_EQ(std::string(r.cmds().back()), std::string("T,19.5"));
  CHECK_EQ(esphome::millis() - 10000, (uint32_t) 50);
}
