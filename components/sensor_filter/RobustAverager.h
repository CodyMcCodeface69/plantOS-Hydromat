/**
 * RobustAverager.h
 *
 * A generic template class for calculating statistically robust averages
 * by collecting a defined number of sensor readings and rejecting outliers.
 *
 * Adapted for ESPHome (ESP-IDF framework).
 *
 * Usage:
 *   RobustAverager<float> avgCalculator(20, 0.10); // 20 samples, reject 10% from each end
 *   avgCalculator.addReading(sensorValue);
 *   if (avgCalculator.isReady()) {
 *     float result = avgCalculator.getRobustAverage();
 *   }
 */

#pragma once

#include <cstdlib>  // for qsort
#include <cstring>  // for memset

namespace esphome {
namespace sensor_filter {

template<typename T>
class RobustAverager {
private:
  T* buffer;                    // Dynamic array to store readings
  int windowSize;               // Number of readings to collect before averaging
  int currentIndex;             // Current position in the circular buffer
  int count;                    // Number of readings collected so far
  float rejectPercentage;       // Percentage of outliers to reject from each end (0.0 to 0.5)

  /**
   * Comparison function for qsort
   * Used internally to sort the buffer for outlier rejection
   */
  static int compareValues(const void* a, const void* b) {
    T arg1 = *static_cast<const T*>(a);
    T arg2 = *static_cast<const T*>(b);
    if (arg1 < arg2) return -1;
    if (arg1 > arg2) return 1;
    return 0;
  }

public:
  /**
   * Constructor
   *
   * @param size Number of readings to buffer before calculating average
   * @param rejectPct Percentage (0.0 to 0.5) of readings to reject from each end.
   *                  Default is 0.10 (10% from bottom, 10% from top = 20% total)
   *                  Example: With 20 readings and 10%, rejects 2 lowest + 2 highest
   */
  RobustAverager(int size, float rejectPct = 0.10f)
    : windowSize(size), currentIndex(0), count(0), rejectPercentage(rejectPct) {

    // Allocate buffer dynamically
    buffer = new T[windowSize];

    // Initialize buffer to zero
    for (int i = 0; i < windowSize; i++) {
      buffer[i] = static_cast<T>(0);
    }

    // Clamp reject percentage to valid range [0.0, 0.5]
    if (rejectPercentage < 0.0f) rejectPercentage = 0.0f;
    if (rejectPercentage > 0.5f) rejectPercentage = 0.5f;
  }

  /**
   * Destructor - free allocated memory
   */
  ~RobustAverager() {
    delete[] buffer;
  }

  /**
   * Add a new reading to the buffer
   *
   * Readings are stored in a circular buffer. Once windowSize readings
   * have been added, new readings overwrite the oldest ones.
   *
   * @param newValue The new sensor reading to add
   */
  void addReading(T newValue) {
    buffer[currentIndex] = newValue;
    currentIndex = (currentIndex + 1) % windowSize; // Circular buffer wraparound

    // Track how many readings we've collected (max = windowSize)
    if (count < windowSize) {
      count++;
    }
  }

  /**
   * Check if enough readings have been collected
   *
   * @return true if the buffer is full and ready to calculate average
   */
  bool isReady() const {
    return count >= windowSize;
  }

  /**
   * Calculate robust average with outlier rejection
   *
   * Algorithm:
   * 1. Verify that the buffer is full (windowSize readings collected)
   * 2. Create a sorted copy of the readings
   * 3. Calculate how many values to reject from each end based on rejectPercentage
   * 4. Reject the smallest N and largest N values (outliers)
   * 5. Calculate the average of the remaining middle values
   * 6. Reset the buffer for the next averaging window
   *
   * Example: With 20 readings and 10% rejection:
   * - Sort all 20 values
   * - Reject 2 smallest and 2 largest (4 total)
   * - Average the middle 16 values
   *
   * @return The robust average value (or 0 if not ready)
   */
  T getRobustAverage() {
    // Return 0 if we don't have enough readings yet
    if (!isReady()) {
      return static_cast<T>(0);
    }

    // Create a temporary copy for sorting (preserve original buffer order)
    T* sortedBuffer = new T[windowSize];
    for (int i = 0; i < windowSize; i++) {
      sortedBuffer[i] = buffer[i];
    }

    // Sort the copy in ascending order
    qsort(sortedBuffer, windowSize, sizeof(T), compareValues);

    // Calculate how many values to reject from each end
    // Example: 20 readings * 0.10 = 2 values from each end
    int rejectCount = static_cast<int>(windowSize * rejectPercentage);

    // Safety: Ensure we reject at least 0 and don't reject everything
    if (rejectCount < 0) rejectCount = 0;
    if (rejectCount * 2 >= windowSize) {
      rejectCount = (windowSize - 1) / 2; // Keep at least 1 value
    }

    // Calculate the range of valid values (middle portion after rejection)
    int startIdx = rejectCount;              // Skip the smallest N values
    int endIdx = windowSize - rejectCount;   // Skip the largest N values
    int validCount = endIdx - startIdx;      // Number of values to average

    // Sum the middle values
    T sum = static_cast<T>(0);
    for (int i = startIdx; i < endIdx; i++) {
      sum += sortedBuffer[i];
    }

    // Calculate average
    T average = sum / static_cast<T>(validCount);

    // Clean up temporary buffer
    delete[] sortedBuffer;

    // Reset for next window (clears count and resets index)
    reset();

    return average;
  }

  /**
   * Reset the buffer to start collecting a new window of readings
   *
   * Called automatically by getRobustAverage() after calculation.
   * Can also be called manually to discard current readings.
   */
  void reset() {
    currentIndex = 0;
    count = 0;
    // Note: Buffer values don't need to be zeroed - they'll be overwritten
  }

  /**
   * Get the current number of readings collected
   *
   * @return Number of readings in the buffer (0 to windowSize)
   */
  int getCount() const {
    return count;
  }

  /**
   * Get the configured window size
   *
   * @return The window size (max number of readings before averaging)
   */
  int getWindowSize() const {
    return windowSize;
  }
};

} // namespace sensor_filter
} // namespace esphome
