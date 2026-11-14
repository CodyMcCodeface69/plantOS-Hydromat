# Control logic: why it works this way

Written 2026-11-14. This is the short, current summary of the three German design specs of
March 2026 that the controller was built from. The originals are in `docs/archive/`
(`pH_Regellogik.md`, `EC_Feeding_Logik.md`, `Steuerungslogik.md`); code comments cite them by
file name and section number.

**What happens in which state is in `FSMINFO.md`, and that file wins.** This note only holds the
reasoning and lists where the code differs from the specs. All values below were read from the
code on 2026-11-14 (`controller.h`, `controller.cpp`, `plantOS.yaml`).

## pH

- In this system pH only drifts **up**: tap water refills raise it, and plants release OH⁻ while
  taking up nitrate. So there is only an acid pump. A pH below the window cannot be corrected and
  raises the `PH_TOO_LOW` alert.
- Drift inside the window is wanted (it sweeps nutrient availability). Correction starts when pH is
  above the day's `ph_max` from the calendar and aims at **5.85** (`PH_CORRECTION_TARGET`), not at
  the middle of the window.
- Dose: `dose_mL = (pH − 5.85) × tank_volume_L × K`, clamped to 0.5–10 mL.
  `K` (mL per L per pH unit) lumps acid strength, buffer capacity and water hardness into one number.
- `K` calibrates itself after each clean cycle: `K_obs = mL / (ΔpH × L)`, then an EMA with α = 0.20,
  clamped to 0.01–0.50, stored in NVS (`PhK`). No update when ΔpH ≤ 0.05, when water was added
  during the cycle, or when the cycle was aborted.
- After dosing the tank is left to mix, then pH is measured again, for at most 5 attempts.
  Measuring waits longer after a refill (10 min) or an EC feeding (5 min) than normally (30 s).

## EC feeding

- The calendar gives, per day, an EC target and tolerance plus the A/B/C doses in mL/L. The calendar
  doses fix the **ratio** between the three pumps and the **upper limit** of one dosing step. They are
  not a daily amount.
- Feeding is triggered by EC, not by time: dose when EC < target − tolerance. No dosing when
  EC > target + 1.5 × tolerance, because high EC after evaporation is fixed by water, not nutrients.
- The intended step size is proportional to the EC gap through a self-learning factor `EC_K_feed`
  (EMA α = 0.20, NVS key `EcKv2`). See "Known problem" below.
- At least 12 h between automatic EC feedings (`EC_MIN_INTERVAL_S`), at most 3 attempts per cycle.
- Order after a refill: fill → EC check and feeding → mix → pH correction → IDLE. pH comes last
  because nutrients shift pH.

## Operation and failures

- Normal operation is autonomous; when a sensor is implausible the system stops what depends on it
  and alerts instead of guessing. Manual actions in the web UI always win.
- Sensor failure handling has three steps: stop the dependent automation, retry every 60 s
  (3 retries), then mark the sensor failed and alert.
- Water temperature only alerts (there is no cooler): warning below 15 °C and above 28 °C,
  critical above 32 °C.
- Alerts go through `alert_service` (INFO / WARNING / CRITICAL, the same alert at most once per
  30 min). The only backend is the log.
- Calibration reminders: pH every 14 days, EC every 30 days.
- Night mode (22:00–09:00 by default, adjustable in the UI) blocks automatic pH correction.

## Where the code differs from the specs

| Topic | Spec (archive) | Code today |
|---|---|---|
| Tank volume | 70 L everywhere | 47.5 L total, 27.4 L between LOW and HIGH (`plantos_hal:`). pH dosing and EC dosing use the 27.4 L delta; feeding during a reservoir change uses the total. |
| Start value of pH `K` | 0.07 | 0.25 (`ph_k_factor` in `plantOS.yaml`), adjustable with the K-factor slider |
| Max acid dose per injection | 5 mL | 10 mL (`PH_MAX_DOSE_ML`) |
| AcidPump limit in the safety gate | 40 s max duration, 900 s minimum interval | 30 s max duration. A minimum interval does not exist in the safety gate. |
| pH mixing time | 10 min for 70 L | Linear in volume, 30 s to 600 s; about 4 min at 27.4 L (`calculatePhMixingDuration()`) |
| Minimum interval between EC feedings | 4 h | 12 h |
| Sensor retries | 3, or 5 in vacation mode | 3 |
| Vacation mode (reduced doses, repeated alerts, daily report) | specified | not implemented |
| Telegram alert backend, acknowledge by reply | specified | not implemented (log backend only) |
| Manual confirmation of grow-phase changes | specified | not implemented; the calendar advances by day |
| pH calibration | 2-point | 3-point (4.00 / 7.00 / 10.01) |

## Known problem: the unit of `EC_K_feed`

Found while checking this note against the code, not fixed here (docs-only change).

- The spec gives the start value 0.15 as **EC rise per dose** (mS/cm per mL/L), but its update
  formula computes **dose per EC rise** (mL/L per mS/cm). The two are reciprocal.
- The code stores `ec_K_feed_ = 0.00015` with clamps 0.00002–0.0005 and learns it as
  `mL/L ÷ µS/cm`, but computes the dose as `delta_ec / ec_K_feed_`
  (`controller.cpp`, EC_CALCULATING). For a gap of 500 µS/cm that is about 3,300,000 mL/L.
- The result is then capped at the calendar dose per pump. So, as far as reading the code shows,
  every EC feeding step doses the **full calendar dose** and the learned factor has no effect.
  Dosing stays bounded by the calendar cap and the attempt limit.
- A fix has to pick one definition, convert the start value and clamps to µS/cm consistently, and
  use a new NVS key.
