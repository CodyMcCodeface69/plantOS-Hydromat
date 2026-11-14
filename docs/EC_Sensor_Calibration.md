# EC Sensor – Calibration & Temperature Compensation Logic

> **Sensor: Keyestudio TDS Meter V1.0 (KS0429)** on GPIO0 + DS18B20 water temperature sensor.
> Implemented in `components/tds_sensor/` (pipeline, calibration factor in NVS) and in the
> controller state `EC_CALIBRATING` (procedure). Checked against the code on 2026-11-14.
> Section numbers are kept stable because code comments cite them (e.g. "§5").
>
> **References:**
> - [Keyestudio KS0429 official docs](https://docs.keyestudio.com/projects/KS0429/en/latest/docs/KS0429.html)
> - [DFRobot_EC GitHub (temperature compensation source)](https://github.com/DFRobot/DFRobot_EC)
> - [GreenPonik ESP32 EC port (12-bit ADC / 3.3V fixes)](https://github.com/GreenPonik/DFRobot_ESP_EC_BY_GREENPONIK)
> - [Random Nerd Tutorials: ESP32 + TDS Sensor (wiring + example code)](https://randomnerdtutorials.com/esp32-tds-water-quality-sensor/)

---

## 1. Units (the March 2026 unit audit is done)

**µS/cm is the only internal unit** for EC: sensor output, thresholds, dosing and the
`plantos/vitals` field `ec`. mS/cm appears only in the calendar JSON (`ec_target`, converted to
µS/cm when parsed) and in display text. The original bug was a µS/cm value treated as mS/cm
(`2596 µS/cm` read as `2596 mS/cm`).

Sensor range: the KS0429 outputs 0–2.3 V. With the polynomial below, 2.3 V is about
2240 µS/cm at 25 °C before the calibration factor. The calendar's highest EC target is
2.2 mS/cm, so the top of the grow schedule sits at the upper end of what this probe can report.

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
   ec_uS_cm    =  tds_ppm * 2.0                   // ppm(500-scale) → µS/cm
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

**Note on temperature coefficient:** Keyestudio uses α = 0.02 (2%/°C). DFRobot uses 0.0185 (1.85%/°C). The code uses 0.02.

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
Outside this range → the controller rejects the calibration (alert `EC_CALIB_ERROR`). `set_calibration_factor()` additionally clamps to this range.

**Persistence:** NVS key `ECCalibFactor` (in `tds_sensor`). Default before first calibration: `1.0`.

---

## 6. Calibration Procedure (State: EC_CALIBRATING)

As implemented in `handleEcCalibrating()` (`controller.cpp`):

```
1. Button "Start EC Calibration" in IDLE → EC_CALIBRATING (no actuators)
2. Prompt: rinse the probe (30 s)
3. Prompt: dip the probe into the reference solution (30 s). Default target 1413 µS/cm,
   changeable with setEcCalibrationTarget()
4. Collect one reading per second, at least 30. The readings count as stable when their
   coefficient of variation is below 5 %; otherwise the oldest 10 are dropped and collection continues
5. factor = target / mean(readings)
6. 0.5 ≤ factor ≤ 2.0 → saved to NVS by tds_sensor (key "ECCalibFactor"), calibration timestamp
   saved (key "EcCalibTs"), back to IDLE
   otherwise → alert EC_CALIB_ERROR, state ERROR, the previous factor stays
```

`resetEcCalibration()` sets the factor back to 1.0.

---

## 7. Implementation

See `components/tds_sensor/tds_sensor.cpp`: `on_adc_update()` buffers voltages,
`update()` takes the median, `apply_temperature_compensation()` and `voltage_to_ec()` are the
pipeline of section 3, and `set_calibration_factor()` clamps to 0.5–2.0 and saves.

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

The controller stores the time of the last EC calibration (`EcCalibTs`) and raises a WARNING
alert when it is older than **30 days** (`EC_CALIB_INTERVAL_S`). Recalibrate also after the probe
was stored dry or cleaned with harsh agents.
