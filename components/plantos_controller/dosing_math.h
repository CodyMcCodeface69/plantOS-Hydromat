#pragma once
// Pure dosing arithmetic of the controller: plain C++ only, no ESPHome includes, so the host
// tests (tests/) call these directly. PlantOSController members keep the guards, logging and
// state; these functions hold the numbers. Behavior notes are in docs/CONTROL_LOGIC.md.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace plantos_controller {
namespace dosing_math {

// ---------------------------------------------------------------------------
// pH
// ---------------------------------------------------------------------------

/// Acid dose in mL for a positive pH excess: ph_delta * tank_L * k, clamped to [min_ml, max_ml].
/// The caller handles ph_delta <= 0 (no dose).
float acidDoseMl(float ph_delta, float tank_L, float k, float min_ml, float max_ml);

/// Result of one K-factor learning step.
struct KUpdate {
    float observed;  ///< K seen in this cycle, clamped to [k_min, k_max]
    float updated;   ///< new K after EMA smoothing
};

/// pH K-factor: observed = ml_total / (ph_delta * tank_L), clamped; updated = EMA (not clamped again).
KUpdate phKUpdate(float ml_total, float ph_delta, float tank_L, float k_current,
                  float alpha, float k_min, float k_max);

/// Linear pH mixing time by tank volume (1.7 L -> 30 s, 70 L -> 600 s), before clamping.
float phMixingDurationRawMs(float volume_L);

/// Trimmed mean: sort, drop 10 % from each end (at least one from each end when there are
/// more than two readings), average the rest. Empty input gives 0.
float robustAverage(const std::vector<float> &readings);

/// Largest absolute difference between any two of three values.
float maxPairwiseDiff(float a, float b, float c);

// ---------------------------------------------------------------------------
// EC
// ---------------------------------------------------------------------------

/// Acceptable EC window around a target: [target - tolerance, target + 1.5 * tolerance].
struct EcWindow {
    float min;
    float max;
};
EcWindow ecWindow(float target, float tolerance);

enum class EcLevel : uint8_t {
    BELOW_RANGE,  ///< ec < min: feeding required
    IN_RANGE,     ///< min <= ec <= max
    ABOVE_RANGE,  ///< ec > max: over-concentration
};
EcLevel classifyEc(float ec, float target, float tolerance);

/// Per-pump doses of one EC-calculated feeding step.
struct EcDoses {
    bool valid;            ///< false when rA + rB + rC <= 0 (the dose fields are then 0)
    float total_ml_per_L;  ///< delta_ec / k
    float a_ml;
    float b_ml;
    float c_ml;
    float dosed_ml_per_L;  ///< (a + b + c) / tank_L, or 0 when tank_L <= 0
};

/// Splits total = delta_ec / k (mL/L) over the pumps by the calendar ratio rA:rB:rC (mL/L) and
/// caps each pump at its calendar dose r * tank_L.
/// NOTE: `delta_ec / k` is the arithmetic the firmware has always used. Whether it should be
/// `delta_ec * k` is an open owner decision; do not change it here.
EcDoses calcEcDoses(float delta_ec, float k, float tank_L, float rA, float rB, float rC);

/// Three pump doses in mL.
struct NutrientDoses {
    float a_ml;
    float b_ml;
    float c_ml;
};

/// Full calendar dose: mL/L per nutrient times the tank volume.
NutrientDoses calendarDoses(float a_ml_per_L, float b_ml_per_L, float c_ml_per_L, float tank_L);

/// One installment of the manual (EC-dependent) feed: a fifth of the full dose.
NutrientDoses installmentDoses(const NutrientDoses &full);

/// EC K-factor: observed = ml_per_L_total / ec_delta, clamped; updated = EMA, clamped again.
KUpdate ecKUpdate(float ml_per_L_total, float ec_delta, float k_current,
                  float alpha, float k_min, float k_max);

/// Population statistics of a reading buffer (EC calibration stability).
struct ReadingStats {
    float mean;
    float std_dev;
    float cv;  ///< std_dev / mean; only meaningful when mean > 0
};
ReadingStats readingStats(const std::vector<float> &readings);

}  // namespace dosing_math
}  // namespace plantos_controller
