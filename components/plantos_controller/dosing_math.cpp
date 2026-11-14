#include "dosing_math.h"

#include <algorithm>
#include <cmath>

// The expressions below were moved verbatim out of PlantOSController (operand order and
// float types included), so results are bit-identical to the pre-split firmware.

namespace plantos_controller {
namespace dosing_math {

float acidDoseMl(float ph_delta, float tank_L, float k, float min_ml, float max_ml) {
    float dose_ml = ph_delta * tank_L * k;
    dose_ml = std::max(min_ml, std::min(max_ml, dose_ml));
    return dose_ml;
}

KUpdate phKUpdate(float ml_total, float ph_delta, float tank_L, float k_current,
                  float alpha, float k_min, float k_max) {
    // K_observed = mL / (pH_delta × volume_L)
    float K_observed = ml_total / (ph_delta * tank_L);

    // Clamp to valid range
    K_observed = std::max(k_min, std::min(k_max, K_observed));

    // EMA smoothing: new_K = alpha * K_observed + (1 - alpha) * K_current
    float K_new = alpha * K_observed + (1.0f - alpha) * k_current;

    return {K_observed, K_new};
}

float phMixingDurationRawMs(float volume_L) {
    // Formula: duration_ms = slope × volume_L + intercept
    // Slope = (600000 - 30000) / (70 - 1.7) = 570000 / 68.3 ≈ 8344.07 ms/L
    // Intercept = 30000 - (8344.07 × 1.7) ≈ 15814.9
    constexpr float SLOPE = 8344.07f;       // ms per liter
    constexpr float INTERCEPT = 15814.9f;   // ms offset
    return SLOPE * volume_L + INTERCEPT;
}

float robustAverage(const std::vector<float> &readings) {
    if (readings.empty()) {
        return 0.0f;
    }

    // Single reading - return as-is
    if (readings.size() == 1) {
        return readings[0];
    }

    // Sort readings for robust averaging
    std::vector<float> sorted = readings;
    std::sort(sorted.begin(), sorted.end());

    // Reject 10% from each end (outliers)
    size_t reject_count = sorted.size() / 10;
    if (reject_count == 0 && sorted.size() > 2) {
        reject_count = 1; // Reject at least 1 from each end
    }

    // Calculate average of middle values
    float sum = 0.0f;
    size_t count = 0;

    for (size_t i = reject_count; i < sorted.size() - reject_count; i++) {
        sum += sorted[i];
        count++;
    }

    if (count == 0) {
        // Fallback: use all readings
        for (float reading : sorted) {
            sum += reading;
        }
        count = sorted.size();
    }

    return sum / count;
}

float maxPairwiseDiff(float a, float b, float c) {
    float diff_1_2 = std::abs(a - b);
    float diff_2_3 = std::abs(b - c);
    float diff_1_3 = std::abs(a - c);

    return std::max({diff_1_2, diff_2_3, diff_1_3});
}

EcWindow ecWindow(float target, float tolerance) {
    float ec_min = target - tolerance;
    float ec_max = target + tolerance * 1.5f;
    return {ec_min, ec_max};
}

EcLevel classifyEc(float ec, float target, float tolerance) {
    const EcWindow window = ecWindow(target, tolerance);
    if (ec > window.max) {
        return EcLevel::ABOVE_RANGE;
    }
    if (ec >= window.min) {
        return EcLevel::IN_RANGE;
    }
    return EcLevel::BELOW_RANGE;  // also where a NaN reading lands, as before
}

EcDoses calcEcDoses(float delta_ec, float k, float tank_L, float rA, float rB, float rC) {
    EcDoses doses{false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    // Total dose in mL/L using adaptive K-factor
    doses.total_ml_per_L = delta_ec / k;

    float r_total = rA + rB + rC;
    if (r_total <= 0.0f) {
        return doses;
    }
    doses.valid = true;

    // Calculate absolute doses per pump, capped at calendar max dose
    doses.a_ml = std::min((rA / r_total) * doses.total_ml_per_L * tank_L, rA * tank_L);
    doses.b_ml = std::min((rB / r_total) * doses.total_ml_per_L * tank_L, rB * tank_L);
    doses.c_ml = std::min((rC / r_total) * doses.total_ml_per_L * tank_L, rC * tank_L);

    // mL/L actually dosed, for the K-factor update after mixing
    float dosed_total_ml = doses.a_ml + doses.b_ml + doses.c_ml;
    doses.dosed_ml_per_L = (tank_L > 0.0f) ? (dosed_total_ml / tank_L) : 0.0f;

    return doses;
}

NutrientDoses calendarDoses(float a_ml_per_L, float b_ml_per_L, float c_ml_per_L, float tank_L) {
    return {a_ml_per_L * tank_L, b_ml_per_L * tank_L, c_ml_per_L * tank_L};
}

NutrientDoses installmentDoses(const NutrientDoses &full) {
    return {full.a_ml / 5.0f, full.b_ml / 5.0f, full.c_ml / 5.0f};
}

KUpdate ecKUpdate(float ml_per_L_total, float ec_delta, float k_current,
                  float alpha, float k_min, float k_max) {
    // K_observed = dose / EC_rise (mL/L per µS/cm)
    float K_observed = ml_per_L_total / ec_delta;

    // Clamp to valid range before EMA
    K_observed = std::max(k_min, std::min(k_max, K_observed));

    // EMA smoothing: new_K = alpha * K_observed + (1 - alpha) * K_current
    float K_new = alpha * K_observed + (1.0f - alpha) * k_current;

    // Final clamp
    K_new = std::max(k_min, std::min(k_max, K_new));

    return {K_observed, K_new};
}

ReadingStats readingStats(const std::vector<float> &readings) {
    float sum = 0.0f;
    for (float r : readings) sum += r;
    float mean = sum / readings.size();

    float sum_sq = 0.0f;
    for (float r : readings) {
        float diff = r - mean;
        sum_sq += diff * diff;
    }
    float variance = sum_sq / readings.size();
    float std_dev = std::sqrt(variance);
    float cv = std_dev / mean;

    return {mean, std_dev, cv};
}

}  // namespace dosing_math
}  // namespace plantos_controller
