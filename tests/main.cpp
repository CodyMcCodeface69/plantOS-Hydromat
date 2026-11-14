// Test runner. Usage: plantos_tests [filter-substring] [--list]
#include <chrono>
#include <cstring>
#include <iostream>
#include "minitest.h"
#include "esphome/core/hal.h"

int main(int argc, char **argv) {
  const char *filter = nullptr;
  bool list = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--list") == 0) list = true;
    else filter = argv[i];
  }

  int run = 0, failed = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (auto &tc : minitest::registry()) {
    std::string full = std::string(tc.group) + "." + tc.name;
    if (filter && full.find(filter) == std::string::npos) continue;
    if (list) {
      std::cout << full << "\n";
      continue;
    }
    host::reset();
    minitest::failures_in_current() = 0;
    try {
      tc.fn();
    } catch (const minitest::Abort &) {
    } catch (const std::exception &e) {
      minitest::fail("<exception>", 0, e.what());
    }
    run++;
    if (minitest::failures_in_current() > 0) {
      failed++;
      std::cerr << "[FAIL] " << full << "\n";
    } else if (getenv("PLANTOS_TEST_VERBOSE")) {
      std::cout << "[ ok ] " << full << "\n";
    }
  }
  if (list) return 0;
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  std::cout << run << " tests, " << minitest::checks_total() << " checks, " << failed << " failed (" << ms
            << " ms)\n";
  if (run == 0) {
    std::cerr << "no tests matched\n";
    return 2;
  }
  return failed == 0 ? 0 : 1;
}
