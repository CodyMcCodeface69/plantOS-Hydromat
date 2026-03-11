# EC Sensor – Calibration & Temperature Compensation Logic

> Reference for implementation in ESPHome / ESP32 C++.
> **Sensor: Keyestudio TDS Meter V1.0 (KS0429)** + DS18B20 water temperature sensor.
>
> **References:**
> - [Keyestudio KS0429 official docs](https://docs.keyestudio.com/projects/KS0429/en/latest/docs/KS0429.html)
> - [DFRobot_EC GitHub (temperature compensation source)](https://github.com/DFRobot/DFRobot_EC)
> - [GreenPonik ESP32 EC port (12-bit ADC / 3.3V fixes)](https://github.com/GreenPonik/DFRobot_ESP_EC_BY_GREENPONIK)
> - [Random Nerd Tutorials: ESP32 + TDS Sensor (wiring + example code)](https://randomnerdtutorials.com/esp32-tds-water-quality-sensor/)

---

## 1. Task for Claude Code: Audit & Fix Unit Mismatch

**Do this before touching anything else.**

The codebase produced readings like `EC 2596.723 mS/cm` which failed plausibility check `[0–5.0]`.
The value is correct in **µS/cm** — it is just never divided by 1000 before being treated as mS/cm.
`2596 µS/cm = 2.596 mS/cm` → perfectly valid for nutrient solution.

**Audit checklist — trace EC through the entire codebase:**

1. Find every location where an EC/TDS value is computed, stored, compared, logged, or passed to dosing logic.
2. Determine the unit at each step (raw ADC → voltage → formula output → variable → plausibility check → dosing logic → display).
3. Fix: establish **µS/cm as the single canonical internal unit** everywhere.
4. Convert to mS/cm **only at the display/logging layer**: `ec_display_mS = ec_internal_uS / 1000.0f`
5. Update all hardcoded thresholds and plausibility bounds to µS/cm (e.g. `[0–5000]` µS/cm instead of `[0–5.0]` mS/cm).
6. Update all dosing logic inputs to expect µS/cm and verify the math still holds.

**Three compounding bugs to look for:**

| Bug | Symptom | Fix |
|-----|---------|-----|
| ppm → µS conversion missing | Values ~2× off | Add `ec_uS = tds_ppm * 2.0f` |
| µS not divided by 1000 | Values ~1000× off in mS context | Add `/1000.0f` at display layer only |
| VREF = 5.0 on ESP32 (should be 3.3V) | Cubic formula blows up non-linearly | Change `VREF` to `3.3f`, ADC resolution to `4095.0f` |

---

## 2. Why Raw ADC → µS is Wrong Without Correction

A direct conversion `analogRead() → voltage → µS` without any correction has two additional errors on top of the unit bug:

1. **No cell constant (K-factor):** Every probe has manufacturing variance. Without it, readings can be off by 10–20%.
2. **No temperature compensation:** EC is strongly temperature-dependent (~2% per °C). At 20°C the reading is ~10% lower than the true 25°C-referenced value.

---

## 3. Full Conversion Pipeline (Keyestudio TDS Meter V1 / KS0429)

The Keyestudio sensor uses a **cubic polynomial** to convert voltage → TDS in ppm.
All internal values must be in **µS/cm**. mS/cm is display-only.

```
analogRead(EC_PIN)
        │
        ▼
   voltage_V   =  raw / 4095.0 * 3.3              // ESP32: 12-bit, 3.3V (NOT 1024/5V!)
        │
        ▼
   compCoeff   =  1.0 + 0.02 * (temperature - 25.0)   // Keyestudio temp coefficient (2%/°C)
   compVoltage =  voltage_V / compCoeff            // temperature-compensated voltage
        │
        ▼
   tds_ppm     =  (133.42 * V³ - 255.86 * V²      // Keyestudio cubic polynomial
                  + 857.39 * V) * 0.5             // output unit: ppm (500-scale, mg/L)
        │
        ▼
   ec_uS_cm    =  tds_ppm * 2.0                   // ppm(500-scale) → µS/cm  [LIKELY MISSING]
        │
        ▼
   ec_uS_cal   =  ec_uS_cm * k_calibration        // apply one-point calibration factor (NVS)
        │
        ▼
   ── all internal logic, thresholds, dosing: use ec_uS_cal (µS/cm) ──
        │
        ▼
   ec_mS_cm    =  ec_uS_cal / 1000.0              // display/logging only
   tds_ppm_out =  ec_uS_cal / 2.0                 // display in ppm if needed
```

**Note on temperature coefficient:** Keyestudio uses α = 0.02 (2%/°C). DFRobot uses 0.0185 (1.85%/°C). Both are fine. Use whichever is already in the codebase — do not mix.

---

## 4. Temperature Compensation Formula

Reference temperature in the EC world is **25°C**. All published nutrient charts assume this.

```
compCoeff  = 1.0 + α * (T - 25.0)
ec_at_25C  = ec_raw / compCoeff
```

Where:
- `α = 0.02` (Keyestudio) or `0.0185` (DFRobot) — pick one, stay consistent
- `T` = measured water temperature in °C from DS18B20
- `ec_raw` = EC reading at actual water temperature
- `ec_at_25C` = EC normalized to 25°C reference

**Effect at common temperatures (α = 0.02):**

| Water Temp | compCoeff | Example: true EC = 1400 µS/cm |
|-----------|-----------|-------------------------------|
| 18°C | 0.86 → ÷0.86 | raw ~1204 → corrected 1400 |
| 22°C | 0.94 → ÷0.94 | raw ~1316 → corrected 1400 |
| 25°C | 1.00 → ÷1.00 | raw 1400  → corrected 1400 |
| 28°C | 1.06 → ÷1.06 | raw ~1484 → corrected 1400 |

---

## 5. One-Point Calibration with 1413 µS/cm Solution

### Why one point is sufficient

Hydroponics operating range: **500–3500 µS/cm**. The 1413 µS/cm standard sits near the center.
A second point (12,880 µS/cm) is only relevant for high-salinity — not needed here.

### Calibration formula

```
k_calibration = 1413.0 / ec_uS_at_25C_measured
```

Where `ec_uS_at_25C_measured` is the fully corrected reading (ppm×2, temperature-compensated) while the probe is in 1413 µS/cm solution.

**Valid range check:** `0.5 ≤ k_calibration ≤ 2.0`
Outside this range → reject, alert "Calibration out of bounds – check probe or solution expiry".

**Persistence:** Store in `NVS("ec_k_cal")`. Default before first calibration: `1.0`.

---

## 6. Calibration Procedure (State: EC_CALIBRATING)

```
1. Enter EC_CALIBRATING state → pause all dosing
2. User immerses probe in 1413 µS/cm reference solution
3. Wait for reading to stabilize (std dev < 10 µS over 10 samples, ~30 s)
4. Read temperature T from DS18B20
5. Compute full pipeline: voltage → ppm → µS/cm → temp-compensated → ec_uS_measured
6. k_new = 1413.0 / ec_uS_measured
7. Validate: 0.5 ≤ k_new ≤ 2.0
      YES → NVS("ec_k_cal") = k_new, NVS("ec_cal_ts") = now()
      NO  → reject, WARNING alert, keep previous k_calibration
8. Return to IDLE / AUTO
```

---

## 7. Complete C++ Implementation Sketch

```cpp
// ── Constants ──────────────────────────────────────────────────────────────
constexpr float EC_TEMP_COEFF     = 0.02f;     // Keyestudio: 2%/°C
constexpr float EC_REF_TEMP       = 25.0f;     // all nutrient charts reference 25°C
constexpr float EC_CAL_TARGET_US  = 1413.0f;   // µS/cm calibration solution
constexpr float EC_K_CAL_MIN      = 0.5f;
constexpr float EC_K_CAL_MAX      = 2.0f;
constexpr float ADC_VREF          = 3.3f;      // ESP32: 3.3V (NOT 5.0V!)
constexpr float ADC_RESOLUTION    = 4095.0f;   // ESP32: 12-bit (NOT 1023!)

// Plausibility bounds — internal unit is µS/cm
constexpr float EC_PLAUSIBLE_MIN  = 0.0f;
constexpr float EC_PLAUSIBLE_MAX  = 5000.0f;   // µS/cm (= 5.0 mS/cm)

// ── Runtime state (NVS-persisted) ──────────────────────────────────────────
float k_calibration = 1.0f;   // loaded from NVS on boot

// ── Main read function — returns µS/cm ─────────────────────────────────────
float readEC_uS(int raw_adc, float temperature_c) {
    // 1. ADC → voltage (ESP32-correct values)
    float voltage_V = (float)raw_adc / ADC_RESOLUTION * ADC_VREF;

    // 2. Temperature compensation (Keyestudio method)
    float compCoeff   = 1.0f + EC_TEMP_COEFF * (temperature_c - EC_REF_TEMP);
    float compVoltage = voltage_V / compCoeff;

    // 3. Keyestudio cubic polynomial → ppm (500-scale)
    float tds_ppm = (133.42f * powf(compVoltage, 3.0f)
                   - 255.86f * powf(compVoltage, 2.0f)
                   + 857.39f * compVoltage) * 0.5f;

    // 4. ppm (500-scale) → µS/cm
    float ec_uS = tds_ppm * 2.0f;

    // 5. Apply calibration K-factor
    ec_uS *= k_calibration;

    return ec_uS;   // ← always µS/cm, never mS/cm
}

// ── Display conversion helpers (call only at logging/display layer) ─────────
float uS_to_mS(float ec_uS)     { return ec_uS / 1000.0f; }
float uS_to_ppm500(float ec_uS) { return ec_uS / 2.0f; }
float uS_to_ppm700(float ec_uS) { return ec_uS / 1000.0f * 700.0f; }

// ── Calibration — input must already be temp-compensated µS/cm ─────────────
bool calibrateEC(float ec_uS_in_cal_solution) {
    float k_new = EC_CAL_TARGET_US / ec_uS_in_cal_solution;
    if (k_new < EC_K_CAL_MIN || k_new > EC_K_CAL_MAX) {
        sendAlert(WARNING, "EC cal rejected: k=" + String(k_new, 3)
                  + " (valid: 0.5–2.0). Check probe & solution.");
        return false;
    }
    k_calibration = k_new;
    saveToNVS("ec_k_cal", k_calibration);
    saveToNVS("ec_cal_ts", currentTimestamp());
    return true;
}
```

---

## 8. Unit Conversion Reference

```
// EC ↔ TDS (ppm)
ec_uS_cm = tds_ppm_500 * 2.0          // ppm(500) → µS/cm
ec_uS_cm = tds_ppm_700 * (1000/700)   // ppm(700) → µS/cm
ec_mS_cm = ec_uS_cm / 1000.0          // µS/cm → mS/cm (display only)

// Inverse
tds_ppm_500 = ec_uS_cm / 2.0
tds_ppm_700 = ec_mS_cm * 700.0
```

Internal code must **never** store or compare in mS/cm. Only the final log/display output converts.

---

## 9. Calibration Reminder

Track `ec_cal_ts` in NVS. Raise `WARNING` alert if:
```
currentDay - ec_cal_last_day > 30
```
Interval: **every 30 days**, or any time the probe has been stored dry or cleaned with harsh agents.
