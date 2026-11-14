// Minimal self-contained test framework (no dependencies).
//
//   TEST(group, name) { CHECK(cond); CHECK_EQ(a, b); CHECK_NEAR(a, b, eps); }
//
// A failing CHECK records the failure and the test continues, so one run shows
// every mismatch. REQUIRE aborts the current test (use when later lines would crash).
#pragma once
#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace minitest {

struct TestCase {
  const char *group;
  const char *name;
  std::function<void()> fn;
};

inline std::vector<TestCase> &registry() {
  static std::vector<TestCase> r;
  return r;
}
inline int &failures_in_current() {
  static int n = 0;
  return n;
}
inline long &checks_total() {
  static long n = 0;
  return n;
}
struct Abort {};

struct Registrar {
  Registrar(const char *group, const char *name, std::function<void()> fn) {
    registry().push_back({group, name, std::move(fn)});
  }
};

inline void fail(const char *file, int line, const std::string &msg) {
  failures_in_current()++;
  std::cerr << "    FAIL " << file << ":" << line << ": " << msg << "\n";
}

// Print anything streamable; 8-bit integers as numbers, not characters.
template<typename T> std::string show(const T &v) {
  std::ostringstream os;
  os.precision(9);
  if constexpr (std::is_enum_v<T>) {
    os << static_cast<long long>(v);
  } else if constexpr (std::is_integral_v<T> && sizeof(T) == 1 && !std::is_same_v<T, bool>) {
    os << static_cast<int>(v);
  } else {
    os << std::boolalpha << v;
  }
  return os.str();
}

}  // namespace minitest

#define TEST(group, name)                                                                 \
  static void minitest_##group##_##name();                                                \
  static ::minitest::Registrar minitest_reg_##group##_##name(#group, #name,               \
                                                             minitest_##group##_##name);  \
  static void minitest_##group##_##name()

#define CHECK(cond)                                                                       \
  do {                                                                                    \
    ::minitest::checks_total()++;                                                         \
    if (!(cond)) ::minitest::fail(__FILE__, __LINE__, "CHECK(" #cond ")");                \
  } while (0)

#define REQUIRE(cond)                                                                     \
  do {                                                                                    \
    ::minitest::checks_total()++;                                                         \
    if (!(cond)) {                                                                        \
      ::minitest::fail(__FILE__, __LINE__, "REQUIRE(" #cond ")");                         \
      throw ::minitest::Abort();                                                          \
    }                                                                                     \
  } while (0)

#define CHECK_EQ(actual, expected)                                                        \
  do {                                                                                    \
    ::minitest::checks_total()++;                                                         \
    auto &&mt_a = (actual);                                                               \
    auto &&mt_e = (expected);                                                             \
    if (!(mt_a == mt_e))                                                                  \
      ::minitest::fail(__FILE__, __LINE__,                                                \
                       std::string("CHECK_EQ(" #actual ", " #expected "): got ") +        \
                           ::minitest::show(mt_a) + ", expected " + ::minitest::show(mt_e)); \
  } while (0)

#define CHECK_NEAR(actual, expected, eps)                                                 \
  do {                                                                                    \
    ::minitest::checks_total()++;                                                         \
    double mt_a = static_cast<double>(actual);                                            \
    double mt_e = static_cast<double>(expected);                                          \
    if (!(std::fabs(mt_a - mt_e) <= static_cast<double>(eps)))                            \
      ::minitest::fail(__FILE__, __LINE__,                                                \
                       std::string("CHECK_NEAR(" #actual ", " #expected "): got ") +      \
                           ::minitest::show(mt_a) + ", expected " + ::minitest::show(mt_e) + \
                           " +/- " + ::minitest::show(static_cast<double>(eps)));         \
  } while (0)
