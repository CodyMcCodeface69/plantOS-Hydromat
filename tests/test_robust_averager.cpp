// sensor_filter::RobustAverager (header-only trimmed mean used for the pH sensor path).
// Real code under test: components/sensor_filter/RobustAverager.h
#include "minitest.h"
#include "RobustAverager.h"

using esphome::sensor_filter::RobustAverager;

TEST(robust_averager, not_ready_until_window_is_full) {
  RobustAverager<float> avg(5, 0.10f);
  CHECK_EQ(avg.getWindowSize(), 5);
  for (int i = 0; i < 4; i++) {
    avg.addReading(6.0f);
    CHECK(!avg.isReady());
  }
  CHECK_EQ(avg.getCount(), 4);
  CHECK_EQ(avg.getRobustAverage(), 0.0f);  // returns 0 when not ready ...
  CHECK_EQ(avg.getCount(), 4);             // ... and keeps the collected readings
  avg.addReading(6.0f);
  CHECK(avg.isReady());
}

TEST(robust_averager, average_resets_the_window) {
  RobustAverager<float> avg(4, 0.0f);
  for (float v : {1.0f, 2.0f, 3.0f, 4.0f}) avg.addReading(v);
  CHECK_NEAR(avg.getRobustAverage(), 2.5f, 1e-6);
  CHECK_EQ(avg.getCount(), 0);
  CHECK(!avg.isReady());
}

TEST(robust_averager, window_of_20_rejects_two_from_each_end) {
  RobustAverager<float> avg(20, 0.10f);
  for (int i = 0; i < 16; i++) avg.addReading(6.0f);
  avg.addReading(0.0f);
  avg.addReading(1.0f);
  avg.addReading(13.0f);
  avg.addReading(14.0f);
  CHECK_NEAR(avg.getRobustAverage(), 6.0f, 1e-6);
}

TEST(robust_averager, small_window_truncates_reject_count_to_zero) {
  // int(5 * 0.10) = 0: with windows below 10 nothing is rejected at the default 10 %.
  // (The controller's own calculateRobustPhAverage rejects at least 1 instead.)
  RobustAverager<float> avg(5, 0.10f);
  for (float v : {6.0f, 6.0f, 6.0f, 6.0f, 16.0f}) avg.addReading(v);
  CHECK_NEAR(avg.getRobustAverage(), 8.0f, 1e-5);
}

TEST(robust_averager, reject_percentage_is_clamped_and_keeps_a_middle_value) {
  RobustAverager<float> odd(5, 0.9f);  // clamped to 0.5 -> reject 2 from each end -> median
  for (float v : {9.0f, 1.0f, 5.0f, 7.0f, 3.0f}) odd.addReading(v);
  CHECK_NEAR(odd.getRobustAverage(), 5.0f, 1e-6);

  RobustAverager<float> even(4, 0.5f);  // 2 * 2 >= 4 -> reject (4 - 1) / 2 = 1 -> mean of middle two
  for (float v : {1.0f, 100.0f, 4.0f, 6.0f}) even.addReading(v);
  CHECK_NEAR(even.getRobustAverage(), 5.0f, 1e-6);

  RobustAverager<float> neg(4, -1.0f);  // clamped to 0 -> plain mean
  for (float v : {1.0f, 2.0f, 3.0f, 6.0f}) neg.addReading(v);
  CHECK_NEAR(neg.getRobustAverage(), 3.0f, 1e-6);
}

TEST(robust_averager, circular_buffer_keeps_latest_window) {
  RobustAverager<float> avg(3, 0.0f);
  for (float v : {100.0f, 1.0f, 2.0f, 3.0f}) avg.addReading(v);  // 100 is overwritten
  CHECK_EQ(avg.getCount(), 3);
  CHECK_NEAR(avg.getRobustAverage(), 2.0f, 1e-6);
}

TEST(robust_averager, reset_restarts_collection) {
  RobustAverager<float> avg(3, 0.0f);
  avg.addReading(9.0f);
  avg.addReading(9.0f);
  avg.reset();
  CHECK_EQ(avg.getCount(), 0);
  for (float v : {1.0f, 2.0f, 3.0f}) avg.addReading(v);
  CHECK_NEAR(avg.getRobustAverage(), 2.0f, 1e-6);
}

TEST(robust_averager, integer_type_uses_integer_division) {
  RobustAverager<int> avg(4, 0.0f);
  for (int v : {1, 2, 2, 2}) avg.addReading(v);
  CHECK_EQ(avg.getRobustAverage(), 1);  // 7 / 4
}
