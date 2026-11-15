# PlantOS Controller - Finite State Machine (FSM) Documentation

**IMPORTANT**: This file is the **authoritative source** for PlantOS Controller FSM behavior. All implementation changes to FSM states, transitions, triggers, or actuator control **MUST** be reflected in this document.

**Location**: `/home/cody/plantOS-testlab/FSMINFO.md`
**Component**: `components/plantos_controller/controller.cpp` and `controller.h`
**Last Updated**: 2026-11-15 (presets)

---

## Overview

The PlantOS Controller implements a unified Finite State Machine (FSM) with 16 states that orchestrate all system behavior including pH correction, nutrient dosing, water management, night mode, and system safety.

**Key Principles**:
- Single unified FSM (replaced old dual-FSM architecture)
- Enum-based state representation with switch-statement dispatch
- All actuator control flows through ActuatorSafetyGate (Layer 2)
- All hardware access flows through HAL (Layer 3)
- Non-blocking timing using `millis()`
- Persistent state recovery via PSM (NVS storage)

---

# State Diagram

```
╔════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╗
║                                                                                                                        ║
║                           PlantOS Controller - Unified FSM State Diagram                                               ║
║                                                                                                                        ║
║  LEGEND:                                                                                                               ║
║    ┌─────────┐   = State Box                                                                                          ║
║    ──────>    = Transition (arrow)                                                                                     ║
║    [trigger]  = Event/condition that causes transition                                                                ║
║                                                                                                                        ║
╚════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════╝

┌──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                          SYSTEM STARTUP & ERROR HANDLING                                             │
└──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

                                            [POWER ON / BOOT]
                                                    │
                                                    ▼
                                         ┌──────────────────────┐
                                         │       INIT           │
                                         │  Boot sequence:      │
                                         │  Red→Yellow→Green    │
                                         │  (3 seconds)         │
                                         │  Check PSM recovery  │
                                         └──────────────────────┘
                                                    │
                      ┌─────────────────────────────┼────────────────────────────┐
                      │ [After 3s]                  │                            │ [After 3s + PSM has STATE_PAUSE]
                      │                             │ [After 3s + PSM has        │
                      │                             │  STATE_SHUTDOWN]           │
                      ▼                             ▼                            ▼
                ┌──────────┐                  ┌───────────┐               ┌───────────┐
                │   IDLE   │◀─────────────────│ SHUTDOWN  │               │   PAUSE   │
                └──────────┘  [setToIdle()]   └───────────┘               └───────────┘
                                              [Solid Yellow]              [Solid Orange]
                                              All actuators OFF           Actuators maintained
                                              Calendar disabled           Calendar disabled
                                              Persists across reboot      Persists across reboot


                                         ┌──────────────────────┐
                                         │       ERROR          │
                                         │  Fast red flash      │
                      ┌─────────────────▶│  All pumps OFF       │──────────────────┐
                      │                  │  (5 seconds)         │                  │
                      │                  └──────────────────────┘                  │
                      │                            │                               │
                      │                            │ [After 5s]                    │
                      │                            ▼                               │
                      │                         [INIT]                             │
                      │                                                            │
                      │ [Various error conditions from other states]               │
                      └────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                              IDLE STATE (Central Hub)                                                │
└──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

                                         ┌──────────────────────┐
                                         │       IDLE           │ ◀────────────────────────┐
                                         │  Breathing green LED │                          │
                                         │  Ready for commands  │                          │ [Night mode hours end OR
                                         │  Periodic pH checks  │                          │  night mode disabled]
                                         │  Air pump health     │                          │
                                         │  monitoring (30s)    │                          │
                                         └──────────────────────┘                          │
                                                    │                                      │
                                                    │ [Night mode enabled &               │
                                                    │  current hour in night mode range]  │
                                                    ▼                                      │
                                         ┌──────────────────────┐                          │
                                         │       NIGHT          │──────────────────────────┘
                                         │  Dim green breathing │
                                         │  No pH/feed/fill     │
                                         │  Manual ops blocked  │
                                         │  Air pump health     │
                                         │  monitoring (30s)    │
                                         └──────────────────────┘
                                                    │
                   ┌───────────────┬────────────────┼────────────────┬────────────────┬──────────────────┐
                   │               │                │                │                │                  │
     [Every 2hrs]  │  [startPh     │  [startPh      │  [startFeed    │  [startFill    │  [startFeed()]   │
     (automatic)   │  Correction   │  Calibration   │  ing()]        │  Tank()]       │                  │
                   │  ()]          │  ()]           │                │                │                  │
                   ▼               ▼                ▼                ▼                ▼                  ▼
            ┌────────────┐   ┌────────────┐  ┌────────────┐  ┌─────────────┐  ┌──────────────┐  ┌──────────────┐
            │     PH_    │   │     PH_    │  │     PH_    │  │   FEEDING   │  │   WATER_     │  │  FEED_       │
            │ PROCESSING │   │ MEASURING  │  │CALIBRATING │  └─────────────┘  │  FILLING     │  │  FILLING     │
            └────────────┘   └────────────┘  └────────────┘                   └──────────────┘  └──────────────┘
                   │
                   │  [setToShutdown()]        [setToPause()]
                   │         │                        │
                   └─────────┼────────────────────────┘
                             ▼                        ▼
                        ┌──────────┐            ┌──────────┐
                        │ SHUTDOWN │            │  PAUSE   │
                        └──────────┘            └──────────┘

┌──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                          pH CORRECTION SEQUENCE                                                      │
│  Flow: PH_PROCESSING → PH_MEASURING → PH_CALCULATING → PH_INJECTING → PH_MIXING → [loop back to PH_MEASURING]      │
└──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

     ┌──────────────────────┐
     │   PH_PROCESSING      │
     │  Yellow pulse        │
     │  Read current pH     │
     │  Check if pH is in   │
     │  target range        │
     └──────────────────────┘
              │
              ├──[pH in range]────────────────────────────────────────────────────────────┐
              │                                                                           │
              ├──[pH < min (too low)]─────────────────────────────────────────────────────┤
              │  WARNING: Cannot raise pH (no base pump)                                  │
              │                                                                           │
              └──[pH > max (too high)]──────────┐                                         │
                                                 ▼                                        │
                                      ┌──────────────────────┐                            │
                                      │   PH_MEASURING       │                            │
                                      │  Yellow pulse        │                            │
                                      │  All pumps OFF       │                            │
                                      │  0.5-min stabilization│                           │
                                      │  pH reading every    │                            │
                                      │  5 seconds           │                            │
                                      │  Temperature comp    │                            │
                                      │  sent to sensor      │                            │
                                      └──────────────────────┘                            │
                                                 │                                        │
                                                 ├──[No readings after 5 min]────────▶ERROR
                                                 │                                        │
                                                 └──[After 5 min]──────┐                  │
                                                                       ▼                  │
                                                            ┌──────────────────────┐      │
                                                            │  PH_CALCULATING      │      │
                                                            │  Yellow fast blink   │      │
                                                            │  Calculate robust    │      │
                                                            │  pH average          │      │
                                                            │  Determine if        │      │
                                                            │  correction needed   │      │
                                                            │  Calculate acid dose │      │
                                                            └──────────────────────┘      │
                                                                       │                  │
          ┌────────────────────────────────────────────────────────────┼──────────────────┤
          │                                                            │                  │
          │ [pH in range]                                              │ [pH too high]    │
          │                                                            │                  │
          │ [pH too low, no base pump]                                 │ [Dose < 0.5mL]   │
          │                                                            │                  │
          │ [Max attempts reached (5)]                                 │                  │
          │                                                            ▼                  │
          │                                                 ┌──────────────────────┐      │
          │                                                 │  PH_INJECTING        │      │
          │                                                 │  Cyan pulse          │      │
          │                                                 │  Turn ON AcidPump    │      │
          │                                                 │  Turn ON AirPump     │      │
          │                                                 │  (for mixing)        │      │
          │                                                 │  Duration: calc'd    │      │
          │                                                 │  from dose (mL)      │      │
          │                                                 └──────────────────────┘      │
          │                                                            │                  │
          │                                                            │ [After dose      │
          │                                                            │  duration +      │
          │                                                            │  200ms margin]   │
          │                                                            ▼                  │
          │                                                 ┌──────────────────────┐      │
          │                                                 │  PH_MIXING           │      │
          │                                                 │  Blue pulse          │      │
          │                                                 │  Turn OFF AcidPump   │      │
          │                                                 │  Keep AirPump ON     │      │
          │                                                 │  (2 minutes mixing)  │      │
          │                                                 └──────────────────────┘      │
          │                                                            │                  │
          │                                                            │ [After 2 min]    │
          │                                                            ▼                  │
          │                                                      [PH_MEASURING]           │
          │                                                      (Loop back to verify)    │
          │                                                                               │
          └───────────────────────────────────────────────────────────────────────────────▶IDLE

┌──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                       pH SENSOR CALIBRATION SEQUENCE                                                 │
│  Flow: PH_CALIBRATING (3-point: Mid 7.00 → Low 4.00 → High 10.01)                                                    │
└──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

     ┌──────────────────────┐
     │  PH_CALIBRATING      │
     │  Yellow fast blink   │
     │                      │
     │  STEP 1/3:           │
     │  Mid-point (pH 7.00) │
     │  - Prompt user (10s) │
     │  - 5 batches × 20    │
     │    readings (1/sec)  │
     │  - back to back (the │
     │    30s wait between  │
     │    batches never ran)│
     │  - Verify stability  │
     │    (±0.1 pH)         │
     │  - Send calib cmd    │
     │                      │
     │  STEP 2/3:           │
     │  Low-point (pH 4.00) │
     │  (same process)      │
     │                      │
     │  STEP 3/3:           │
     │  High (pH 10.01)     │
     │  (same process)      │
     └──────────────────────┘
              │
              ├──[Sensor not ready]──────────────▶ERROR
              │
              ├──[Calibration failure]───────────▶ERROR
              │
              └──[All 3 points complete]─────────▶IDLE

┌──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                         FEEDING SEQUENCE                                                             │
│  Flow: FEEDING (sequential nutrient pumps A → B → C) → [optional auto pH]                                            │
└──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

     ┌──────────────────────┐
     │     FEEDING          │
     │  Orange pulse        │
     │  Send temp comp      │
     │                      │
     │  Get nutrient doses  │
     │  from CalendarMgr    │
     │  (mL/L × tank vol)   │
     │                      │
     │  Step 1: Pump A      │
     │  Turn ON Nutrient    │
     │  PumpA for calc'd    │
     │  duration            │
     │  (grow nutrients)    │
     │                      │
     │  Step 2: Pump B      │
     │  Turn ON Nutrient    │
     │  PumpB for calc'd    │
     │  duration            │
     │  (micronutrients)    │
     │                      │
     │  Step 3: Pump C      │
     │  Turn ON Nutrient    │
     │  PumpC for calc'd    │
     │  duration            │
     │  (bloom nutrients)   │
     └──────────────────────┘
              │
              ├──[SafetyGate rejects]────────────▶ERROR
              │
              └──[All pumps complete]────────────┐
                                                 │
                          ┌──────────────────────┼─────────────────────┐
                          │                      │                     │
                          │ [auto_ph_          [No auto pH]            │
                          │  correction_                               │
                          │  pending = true]                           │
                          ▼                                            ▼
                   ┌────────────┐                                   [IDLE]
                   │     PH_    │
                   │ PROCESSING │
                   └────────────┘

┌──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                     WATER MANAGEMENT SEQUENCES                                                       │
└──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
WATER FILLING (standalone)
═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

     ┌──────────────────────┐
     │   WATER_FILLING      │
     │  Blue solid          │
     │                      │
     │  SAFETY CHECK:       │
     │  Water level sensors │
     │  must be available   │
     │  (no blind fill)     │
     │                      │
     │  Turn ON WaterValve  │
     │  (ASG backstop 600s) │
     │                      │
     │  Monitor:            │
     │  - Water level HIGH  │
     │    sensor            │
     │  - Timeout (10 min)  │
     └──────────────────────┘
              │
              ├──[Sensors unavailable]───────────▶IDLE
              │  (SAFETY ABORT, alert             (Clear flags & PSM)
              │   HARDWARE_WATER_SENSORS_MISSING)
              │
              ├──[SafetyGate rejects]────────────▶IDLE
              │
              ├──[HIGH sensor ON]────────────────┐
              │  Close valve                     │
              │  Clear PSM                       │
              │                                  │
              ├──[Timeout (10 min backup)]───────┤
              │  Close valve                     │
              │  Clear PSM                       │
              │  Alert: HIGH sensor never        │
              │  triggered                       │
              │                                  │
              └──────────────────────────────────┘
                                                 │
                          ┌──────────────────────┼────────────────┐
                          │ [auto_ph_          [No auto pH]       │
                          │  correction_                          │
                          │  pending = true]                      │
                          ▼                                       ▼
                   ┌────────────┐                              [IDLE]
                   │     PH_    │
                   │ PROCESSING │
                   └────────────┘

═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
WATER EMPTYING (via Shelly or manual)
═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

     ┌──────────────────────┐
     │   WATER_EMPTYING     │
     │  Purple pulse        │
     │  Check LOW sensor    │
     │  before starting     │
     │                      │
     │  Turn ON Wastewater  │
     │  Pump (ASG backstop  │
     │  1800s)              │
     │                      │
     │  Monitor:            │
     │  - Water level EMPTY │
     │    sensor OFF        │
     │  - Timeout (300s)    │
     └──────────────────────┘
              │
              ├──[Tank already empty (LOW OFF)]──▶IDLE
              │
              ├──[SafetyGate rejects]────────────▶IDLE
              │
              ├──[EMPTY sensor OFF]──────────────┐
              │  Stop pump                       │
              │  Clear PSM                       │
              │                                  │
              ├──[Timeout (300s fallback)]───────┤
              │  Stop pump                       │
              │  Clear PSM                       │
              │                                  │
              └──────────────────────────────────▶IDLE

═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
FEED OPERATION (complete sequence: Fill → Nutrients → pH)
═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

     ┌──────────────────────┐
     │   FEED_FILLING       │
     │  Blue solid          │
     │  First phase of Feed │
     │  operation           │
     │                      │
     │  SAFETY CHECKS:      │
     │  1. Water level      │
     │     sensors must be  │
     │     available (no    │
     │     blind fill)      │
     │  2. Verify tank      │
     │     empty (all 3     │
     │     sensors OFF)     │
     │                      │
     │  Turn ON WaterValve  │
     │  (calc'd max time    │
     │  based on tank vol   │
     │  and valve flow +    │
     │  20% margin)         │
     │                      │
     │  Monitor:            │
     │  - Water level HIGH  │
     │    sensor            │
     │  - Timeout (calc'd)  │
     └──────────────────────┘
              │
              ├──[Sensors unavailable]───────────▶IDLE
              │  (SAFETY ABORT, alert             (Clear flags & PSM)
              │   HARDWARE_WATER_SENSORS_MISSING)
              │
              ├──[Tank not empty]────────────────▶IDLE
              │  (sensors ON)                    (Clear flags & PSM)
              │
              ├──[SafetyGate rejects]────────────▶IDLE
              │                                   (Clear flags & PSM)
              │
              ├──[HIGH sensor ON]────────────────┐
              │  Close valve                     │
              │  Set auto_ph_correction_         │
              │  pending = true                  │
              │                                  │
              └──[Timeout (fallback)]────────────┤
                 Close valve                     │
                 (overflow prevention)            ▼
                                          ┌──────────────┐
                                          │   FEEDING    │
                                          └──────────────┘
                                                  │
                                                  │ [After all 3
                                                  │  nutrient pumps]
                                                  ▼
                                          ┌────────────┐
                                          │     PH_    │
                                          │ PROCESSING │
                                          └────────────┘
                                                  │
                                                  ▼
                                               [IDLE]
```

---

# Reference Tables

## Actuator Actions by State

| STATE              | ACTUATOR ACTIONS |
|-------------------|------------------|
| INIT               | - None (LED only: Red→Yellow→Green)<br>- At 3 s: PSM recovery may switch WaterValve / WastewaterPump OFF (table "PSM Events") |
| IDLE               | - None (LED only: Breathing Green) |
| SHUTDOWN           | - Turn OFF all actuators (emergency safety) |
| PAUSE              | - Maintain current actuator states (no changes) |
| ERROR              | - Turn OFF all pumps (safety) |
| PH_PROCESSING      | - None (read pH sensor only) |
| PH_MEASURING       | - Turn OFF all pumps (for accurate pH reading) |
| PH_CALCULATING     | - None (calculation only) |
| PH_INJECTING       | - Turn ON AcidPump (duration: calculated dose in mL → seconds)<br>- AirPump: **DISABLED** (test results showed no benefit; re-enable if mixing drift observed) |
| PH_MIXING          | - Turn OFF AcidPump<br>- AirPump: **DISABLED** (same as above; cycling restore also commented out) |
| PH_CALIBRATING     | - None (sensor calibration only, no actuators) |
| EC_CALIBRATING     | - None (sensor calibration only, no actuators) |
| FEEDING            | **Setup state only — immediately delegates to EC_FEEDING loop.**<br>- Calculates individual dose = (calendar_mL/L × tank_volume × dose_multiplier) / 5<br>- If EC target configured: sets `feeding_manual_mode_=true`, transitions to EC_FEEDING<br>- If no EC target: uses full calendar dose in single pass via EC_FEEDING<br>**EC_FEEDING → EC_MIXING → EC_MEASURING loop repeats until EC target reached or 20 installments** |
| WATER_FILLING      | - Turn ON WaterValve (until HIGH sensor or 10-min timeout; ASG backstop 600s)<br>- Opened once per entry, on the first handler call after the transition (entry flag reset by `transitionTo`; until 2026-11-14 only within the first 100 ms and skipped after a fill cut short by stop/pause)<br>- **Aborts to IDLE if water level sensors unavailable (no blind fill)** |
| WATER_EMPTYING     | - Turn ON WastewaterPump (until EMPTY sensor OFF or 300s timeout; ASG backstop 1800s)<br>- Started once per entry, on the first handler call (same entry flag as WATER_FILLING) |
| FEED_FILLING       | - Turn ON WaterValve (calc'd duration, or until HIGH sensor)<br>- **Aborts to IDLE if water level sensors unavailable (no blind fill)** |

## Persistent State Manager (PSM) Events

The event names are the constants in `components/persistent_state_manager/psm_events.h`; `logEvent()`,
the recovery, the IDLE/NIGHT hold check and `clearPsmEventIf()` all use them. Until 2026-11-15 the
recovery compared against `WATER_FILLING` / `WATER_EMPTYING`, which no code ever logged, so a crash
mid-fill or mid-drain fell through to "alert only" and the drain pump (a Shelly socket that keeps
running through an ESP reset) was never switched off by the recovery.

**When:** once, at the end of INIT (`elapsed >= 3 s`, `recoverFromPsm()`), when every component is set up.
(Before 2026-11-15 only while INIT was < 100 ms old, a window the first `loop()` after boot usually
misses; the IDLE/NIGHT hold check below then caught SHUTDOWN/PAUSE about 5 s later.)

| LOGGED BY | PSM EVENT (status) | RECOVERY ACTION ON BOOT |
|-------------------|---------------------------|------------------------|
| SHUTDOWN entry     | `STATE_SHUTDOWN` (1)      | INIT → SHUTDOWN, alert "State restored: SHUTDOWN" |
| PAUSE entry        | `STATE_PAUSE` (1)         | INIT → PAUSE, alert "State restored: PAUSE" |
| startFillTank()    | `WATER_FILL` (0)          | WaterValve OFF, PSM cleared, alert, INIT → IDLE |
| startEmptyTank()   | `WATER_EMPTY` (0)         | WastewaterPump OFF (repeated once when the Shelly answers), PSM cleared, alert, INIT → IDLE |
| startFeed()        | `FEED_OPERATION` (0)      | WaterValve OFF, PSM cleared, alert, INIT → IDLE |
| startReservoirChange() | `RESERVOIR_CHANGE` (0) | WaterValve OFF + WastewaterPump OFF (drain repeated as above), PSM cleared, alert, INIT → IDLE |
| startPhCorrection(), auto pH, post-feed pH | `PH_CORRECTION` (0) | Alert only, event kept, INIT → IDLE |
| pH < 5.0 or > 7.5 while measuring | `PH_CRITICAL` (2) | Alert only, event kept; cleared by the next good measurement |
| startPhCalibration() | `PH_CALIBRATION` (0)    | Alert only, event kept, INIT → IDLE |
| startFeeding()     | `FEEDING` (0)             | Alert only, event kept, INIT → IDLE |
| EC feeding loop    | `EC_FEEDING` (0)          | Alert only, event kept, INIT → IDLE |
| startMotherNutrients() | `MOTHER_MIX` (0)      | Alert only, event kept, INIT → IDLE |

- The alert for an interrupted operation is WARNING "Operation interrupted" with the event name and age.
- The OFF commands go through the safety gate with `forceExecute` (after a reset the gate tracks every
  actuator as OFF and would drop a plain OFF as "already requested"). OFF always passes the preset
  block, so this is the same in every preset. Dosing pumps and the valve are GPIO/LEDC outputs and are
  off after a reset anyway; the explicit valve OFF is a second line.
- At INIT WiFi is usually not up, so the first Shelly OFF for the drain pump may fail. `resendRecoveryDrainOff()`
  (every `loop()`) sends it once more when the Shelly is reachable, unless a new drain (WATER_EMPTYING) runs.
  The last backstop stays the gate's max duration: when the Shelly poll reports the pump ON, the gate starts
  tracking it and switches it off after 1800 s.
- IDLE/NIGHT hold check (every 5 s): `STATE_SHUTDOWN` / `STATE_PAUSE` in the PSM → SHUTDOWN / PAUSE.

## External Triggers (Public API)

### Manual Triggers

Every `start*()` below also needs the functions of the active preset (see "Presets", capability table).

| PUBLIC METHOD              | PRECONDITION       | TRANSITION TO        | DESCRIPTION |
|---------------------------|--------------------|----------------------|-------------|
| startPhCorrection()        | Must be in IDLE    | PH_MEASURING         | Manual pH correction (web UI button)<br>**Blocked in NIGHT state** |
| startPhCalibration()       | Must be in IDLE    | PH_CALIBRATING       | 3-point sensor calibration (web UI) |
| startEcCalibration()       | Must be in IDLE<br>EC sensor must have value | EC_CALIBRATING | Single-point EC calibration (1413 uS/cm, web UI) |
| setEcCalibrationTarget(x)  | Any state          | (no transition)      | Set target uS/cm for EC calibration (default: 1413) |
| resetEcCalibration()       | Any state          | (no transition)      | Reset EC factor to 1.0 (factory default) |
| startFeeding()             | Must be in IDLE    | FEEDING → EC_FEEDING | Manual EC-dependent nutrient dosing (web UI button)<br>Doses in installments of (calendar_total × multiplier) / 5<br>Loops via EC_MIXING → EC_MEASURING until EC target reached (max 20 installments)<br>Falls back to single-pass if no EC target configured<br>**Blocked in NIGHT state** |
| startMotherNutrients(L)    | Must be in IDLE    | EC_FEEDING → IDLE    | Dose nutrients for mother plant into a separate container<br>Fixed rates: A=1.44 mL/L, B=0.96 mL/L, C=0.48 mL/L<br>Single pass A→B→C only — no EC loop, no EC_MIXING, no pH correction<br>Volume: user-specified 1–15 L (WebUI number input 04_17)<br>Sets `is_mother_mix_=true` to short-circuit EC_FEEDING→IDLE |
| startFillTank()            | Must be in IDLE<br>Water level sensors available | WATER_FILLING        | Fill tank → EC check → pH correction (post-fill sequence)<br>**Rejected with error if water level sensors unavailable (overflow risk)**<br>**Blocked in NIGHT state** |
| startEmptyTank()           | Must be in IDLE<br>Shelly integration enabled | WATER_EMPTYING       | Drain via WastewaterPump (Shelly socket 2) → IDLE<br>**Rejected with error if Shelly integration disabled**<br>**Blocked in NIGHT state** |
| startFeed()                | Must be in IDLE<br>Water level sensors available<br>Tank must be empty | FEED_FILLING         | Complete feed: Fill→Nutrients→pH<br>(safety checks: sensors available, all 3 sensors OFF)<br>**Blocked in NIGHT state** |
| requestPreset(i) / requestPresetByName() | IDLE, NIGHT, SHUTDOWN, PAUSE or ERROR | (no transition) | Switch preset, see "Presets" |
| setPresetFunction(), setPresetVolumeDelta/Total(), restartGrow() | IDLE, NIGHT, SHUTDOWN, PAUSE or ERROR | (no transition) | Edit the active preset / restart its grow at day 1 |
| startReservoirChange()     | Must be in IDLE<br>Shelly integration enabled | WATER_EMPTYING       | Drain → FEED_FILLING → FEEDING → pH correction (total tank volume)<br>**Rejected with error if Shelly integration disabled**<br>**Blocked in NIGHT state** |
| setToShutdown()            | Any state          | SHUTDOWN             | Emergency shutdown (persists) |
| setToPause()               | Any state          | PAUSE                | Pause system (persists) |
| setToIdle()                | SHUTDOWN or PAUSE  | IDLE                 | Resume from SHUTDOWN/PAUSE |
| resetToInit()              | Any state          | INIT                 | Reset to boot sequence |

### Automatic Triggers

| AUTOMATIC TRIGGER         | PRECONDITION       | TRANSITION TO        | DESCRIPTION |
|---------------------------|--------------------|----------------------|-------------|
| Every 2 hours (in IDLE)    | System in IDLE<br>**Auto pH correction enabled** (switch 04_10)<br>**Not in night mode hours** | PH_PROCESSING        | Periodic pH monitoring (**NOT triggered in NIGHT or when night mode hours are active**)<br>Skipped silently when 04_10 switch is OFF or 22:00–09:00 |
| **Automatic Feeding** ⚡   | **Water level LOW (HIGH=OFF, LOW=OFF, EMPTY=ON)**<br>Active preset has Water Fill + Nutrients<br>System in IDLE<br>Auto-feeding enabled (switch **04_11**)<br>NOT in NIGHT/SHUTDOWN/PAUSE<br>NOT already fed today (NVS check) | **FEED_FILLING → FEEDING → EC_FEEDING** | **Once-per-day automatic feeding when water reaches LOW level**<br>Stores the local date (days since 1970-01-01) in its own NVS key, per preset: `AutoFeedDay` (Haupt), `AutoFeedDay1` (Mutter), `AutoFeedDay0` (Anzucht) (int32, written only when it changes)<br>Prevents duplicate feeds after power cycles<br>Resets at local midnight (same date as the calendar day advance)<br>Toggle via Web UI switch: **04_11_Auto EC/Feeding Correction** |
| Auto-fill (level LOW) | System in IDLE<br>"Auto Water Level" switch on<br>Active preset has Water Fill<br>Not in night mode hours<br>Not triggered by auto-feed first | WATER_FILLING → (EC check) → pH | Checked after auto-feed in `checkAutoTriggers()`; tops up a preset that has water fill but no nutrient pumps |
| Auto reservoir change (weekly) | System in IDLE<br>Active preset has Drain + Water Fill + Nutrients<br>Auto reservoir change enabled (switch)<br>Not in night mode hours<br>Local weekday == configured day (0=Sun..6=Sat)<br>Not already done this local week (NVS `AutoResWeek`) | WATER_EMPTYING → FEED_FILLING → FEEDING → pH | Weekday and week index come from the LOCAL date (`getLocalDateOrdinal()`, same as night mode and the feed date; UTC-based until 2026-11-14). The week index is `local_days / 7` (rolls over on Thursdays). The week is stored in its own NVS key, per preset: `AutoResWeek` (Haupt), `AutoResWeek1`, `AutoResWeek0` (int32, written only when it changes) |
| Night mode hours start     | IDLE + night mode enabled + current hour in range | NIGHT | Automatic transition to night mode |
| Night mode hours end       | NIGHT + (night mode disabled OR hour out of range) | IDLE | Automatic transition back to idle |
| PSM recovery check         | Boot from INIT     | SHUTDOWN or PAUSE    | Restore persisted state after power loss |
| Time-based grow light      | Calendar + Time<br>"Grow Light Schedule" on<br>Active preset has Grow Light | (no state change)    | Control GrowLight via HAL based on schedule |

## State Timeout Values

| STATE              | TIMEOUT/DURATION           | ACTION AFTER TIMEOUT |
|-------------------|---------------------------|---------------------|
| INIT               | 3 seconds                  | → IDLE (or SHUTDOWN/PAUSE if PSM recovery) |
| IDLE               | No timeout                 | Continuous (automatic → NIGHT if night mode hours) |
| NIGHT              | No timeout                 | Continuous (automatic → IDLE when hours end) |
| ERROR              | 5 seconds                  | → INIT (automatic recovery) |
| PH_MEASURING       | 30s (normal) / 5min (post EC feeding) / 10min (post water fill) | → PH_CALCULATING |
| PH_INJECTING       | Calculated dose + 200ms    | → PH_MIXING |
| PH_MIXING          | Tank-volume based, 30–600 s (`calculatePhMixingDuration`); without the preset's air pump × `no_air_pump_settle_factor` (max 20 min) | → PH_MEASURING (loop back to verify) |
| EC_MIXING          | 300 s; without the preset's air pump × `no_air_pump_settle_factor` (max 20 min) | → EC_MEASURING |
| WATER_FILLING      | 10min (safety backup) or HIGH sensor | → IDLE (sets `auto_ec_check_pending_`; IDLE dispatches EC_PROCESSING when not night mode)<br>Immediate abort → IDLE if water level sensors unavailable |
| WATER_EMPTYING     | 300s (fallback) or EMPTY sensor OFF | → IDLE |
| FEED_FILLING       | Calculated or sensor       | → FEEDING<br>Immediate abort → IDLE if water level sensors unavailable |
| EC_CALIBRATING     | ~2.5 min nominal (30s+30s+30s+30s + readings) | → IDLE on COMPLETE, → ERROR on failure |

---

# Safety Features in FSM

## 1. ActuatorSafetyGate Integration (Layer 2)
All actuator commands flow through ActuatorSafetyGate which provides:
- **Debouncing**: Prevents redundant commands
- **Duration limits**: Enforces max runtime per actuator
- **Soft-start/soft-stop**: PWM ramping to protect hardware

## 2. Critical pH Monitoring
- If pH < 5.0 or pH > 7.5: log to PSM, add **PH_CRITICAL** alert, continue measuring
- pH range checked in PH_CALCULATING before correction
- Max 5 correction attempts before aborting to IDLE
- **PH_TOO_LOW** alert raised when pH is below target_min and cannot be corrected (no base pump)
  - Alert clears automatically when pH returns to within target range
  - Triggered in PH_CALCULATING, persists until next successful pH correction cycle

## 3. Water Level Sensor Monitoring (3-Sensor System)

### Hardware Configuration
- **GPIO17**: HIGH sensor (XKC-Y23-V) - Tank full indicator
- **GPIO23**: LOW sensor (XKC-Y23-V) - Auto-feed trigger level (physically moved upward)
- **GPIO22**: EMPTY sensor (XKC-Y23-V) - Minimum safe level (prevents pump dry-running)
- **Tank Volume**: 1.7L (LOW to HIGH sensor range) - used for nutrient dose calculations

### 5 Water Level States

| HIGH | LOW | EMPTY | State | Description | Auto-Feed Trigger |
|------|-----|-------|-------|-------------|-------------------|
| ON | ON | * | **FULL** | Above HIGH sensor | No |
| OFF | ON | * | **NORMAL** | Between HIGH and LOW | No |
| OFF | OFF | ON | **LOW** ⚡ | Between LOW and EMPTY | **YES** (once per day) |
| OFF | OFF | OFF | **EMPTY** ⚠️ | Below EMPTY sensor | No (danger zone) |
| ON | OFF | * | **ERROR** | Invalid sensor state | No (check wiring) |

### State-Specific Safety Actions
- **WATER_FILLING**: Abort on HIGH sensor ON (prevent overflow)
- **WATER_FILLING / FEED_FILLING**: Refuse to start if water level sensors unavailable — no blind time-based filling (overflow risk); raises `HARDWARE_WATER_SENSORS_MISSING` alert and aborts to IDLE
- **WATER_EMPTYING**: Abort on EMPTY sensor OFF (prevent dry pump)
- **WATER_EMPTYING**: Refuse to start if the Shelly integration is disabled (web UI switch "Shelly Integration"). If it is disabled mid-drain, the HAL queues WastewaterPump OFF and the state aborts to IDLE (reservoir change cancelled, PSM event cleared)
- **FEED_FILLING**: Pre-check all 3 sensors OFF (tank must be completely empty)
- **AUTOMATIC FEEDING**: Triggers at LOW state (HIGH=OFF, LOW=OFF, EMPTY=ON), max once per day

## 4. Persistent State Recovery (PSM)
- SHUTDOWN and PAUSE states persist across power loss
- Water operations (fill, drain, feed, reservoir change) switch the valve / drain pump OFF on boot recovery (table "PSM Events")
- All operations logged with timestamps for age tracking

## 5. Maximum Attempt Limits
- pH correction: max 5 attempts before aborting to IDLE
- Prevents infinite loops in case of dosing errors

## 6. Temperature Compensation
Sent to pH sensor before all critical readings:
- PH_MEASURING (5-min stabilization)
- PH_CALIBRATING (all 3 points)
- FEEDING (before nutrient dosing)

## 7. Error State Transitions
- Any critical failure → ERROR state (5s display, then → INIT)
- All pumps turned OFF in ERROR state for safety
- SafetyGate rejection → appropriate recovery (ERROR or IDLE)

## 8. Night Mode Protection
**NIGHT State** prevents all operations during configured night hours:
- **Configuration**: Enable/disable via web UI, set start/end hours (0-23)
- **Automatic Transition**: IDLE ↔ NIGHT based on current time and configuration
- **Blocked Operations**: pH correction, feeding, water filling/emptying, reservoir change
- **LED Indicator**: Dim breathing green (5-30% brightness vs. 10-100% in IDLE)
- **Manual Override**: All manual operations return warning "Cannot start X - system in NIGHT mode"
- **Periodic pH Checks**: Disabled during night mode (no automatic PH_PROCESSING trigger)
- **Time Source Required**: Needs NTP time synchronization to determine current hour
- **Default Configuration**: **Enabled (on)**, 22:00 start, **09:00 end**
- **Web UI Controls**:
  - Switch: "Night Mode Enabled" (toggle on/off, **default ON**)
  - Number: "Night Mode Start Hour" (0-23, default: 22)
  - Number: "Night Mode End Hour" (0-23, default: **9**)

## 9. Automatic Feeding Daily Limit ⚡
**Prevents excessive feeding through NVS-backed daily tracking**:
- **Trigger Condition**: Water level reaches LOW state (HIGH=OFF, LOW=OFF, EMPTY=ON)
- **Daily Limit**: Maximum 1 automatic feed per local calendar day (local midnight to local midnight)
- **NVS Tracking**: Stores the local date ordinal (days since 1970-01-01) under the NVS key `AutoFeedDay` (int32).
  It is separate from the single PSM event slot, so the `FEED_OPERATION` event that `startFeed()` logs does not
  erase it. Written only when the day changes (at most once a day). Until 2026-11-14 the marker was an
  `AUTOFEED_<ts>` PSM event that `startFeed()` overwrote at once, so a reboot on a fed day could feed twice.
- **Power Cycle Recovery**: `last_auto_feed_day_` is read from NVS on the first trigger check after boot
- **Time Source Required**: Needs NTP synchronization to know the local date (`getLocalDateOrdinal()`)
- **Blocked States**: NIGHT, SHUTDOWN, PAUSE, ERROR states prevent automatic feeding
- **Web UI Control**: Switch "04_11_Auto EC/Feeding Correction" (default: ON, persisted to NVS)
- **Safety**: Prevents overfeeding if water level fluctuates near LOW threshold multiple times per day
- **Implementation**: `shouldTriggerAutoFeeding()` checks 7 conditions before triggering feed

---

# Presets (Anzucht / Mutter / Haupt)

Added 2026-11-15 (overhaul phase 3). Code: `components/plantos_controller/presets.cpp` (switch, edits,
capability table, refusals), `grow_profile.{h,cpp}` (NVS store), `preset_model.h` (pure model, host-tested).

## Model

One firmware, three presets, chosen at runtime in the web UI ("Preset" select) and kept in NVS.
A preset has:

| Part | Where | Editable |
|---|---|---|
| Tank volume LOW→HIGH (delta) and EMPTY→HIGH (total) | `plantos_controller: presets:` (YAML default), NVS record per preset | UI numbers "Tank Volume LOW→HIGH" / "Tank Volume EMPTY→HIGH" (active preset) |
| Function mask (`NUTRIENTS`, `WATER_FILL`, `DRAIN`, `AIR_PUMP`, `GROW_LIGHT`) | same | UI switches "Function ..." (active preset) |
| Calendar (phases, light window, doses, EC target) | `calendar_manager: calendars:` (same name) | YAML only (reflash) |
| Grow day / sync date | calendar_manager slot per calendar (NVS) | "Restart Grow (Day 1)", "Calendar Day +1/-1" |

pH measurement and pH correction (probe + acid pump) are always available. Global, not per preset:
night mode, auto switches (Auto pH / Auto EC/Feeding / Auto Water Level / Auto Reservoir Change),
pump flow rates and PWM, Shelly wiring, pH and EC K-factors.

Defaults: **Haupt** = all functions, 27.4 / 47.5 L (the production values). **Mutter** = Nutrients +
Water Fill + Drain, 10 / 15 L (placeholder). **Anzucht** = no function (pH only), 5 / 8 L (placeholder).
First boot of the preset firmware: no NVS record → Haupt with the YAML values = the behavior before presets.

The active preset's volumes are pushed into the HAL (`setTankVolumeDelta/Total`), so every dosing
formula reads them unchanged through `getTankVolumeDelta()` / `getTotalTankVolume()` / `getTankVolume()`.

## Capability table (the one place: `operationFunctions()`)

| Operation (start*) | Needs |
|---|---|
| pH correction, pH calibration, EC calibration | – |
| Feeding ("Add Nutrients"), EC check ("EC Correction"), mother mix | Nutrients |
| Fill tank | Water Fill |
| Empty tank | Drain |
| Feed (fill + nutrients + pH) | Water Fill + Nutrients |
| Reservoir change (drain + fill + nutrients + pH) | Drain + Water Fill + Nutrients |

`canStart()` checks state first ("night mode" / "busy"), then the mask, then the hardware (sensors,
Shelly). A refusal reads `not available in preset Mutter (needs Drain)`; it is logged
(`Cannot start ...`) and shown in the "Last Refusal" text sensor (`HH:MM <operation>: <reason>`).

Automatic triggers use the same table, silently (no refusal entry):

| Trigger | Without the function |
|---|---|
| Periodic pH check | always runs |
| Auto-feed (level LOW, once a day) | needs Feed (Water Fill + Nutrients); otherwise not triggered and no feed day marked. A preset with Water Fill but no Nutrients tops up through **auto-fill** (if "Auto Water Level" is on), never a half feed |
| Auto-fill (level LOW) | needs Water Fill |
| Auto reservoir change (weekly) | needs Drain + Water Fill + Nutrients |
| Post-fill EC check (EC_PROCESSING) | without Nutrients: skipped, the chain goes on to the pH correction (`transitionAfterEcSkipped()`) |
| Grow light schedule (`controlGrowLight`) | without Grow Light the light is never commanded. The calendar's light window still defines "night" for night mode |
| Air pump "normal mode" re-activation (HAL Shelly poll), WiFi-connect activation | without Air Pump nothing is sent |

Second line of defense: the controller pushes the mask into the ActuatorSafetyGate
(`setActuatorBlocked`). For a blocked actuator the gate rejects ON commands, air pump patterns and
"air pump on"; OFF always passes. Manual actuator switches and actuator test buttons are refused
by the controller first (with the Last Refusal reason). The AcidPump is never blocked.
Emergency stop, shutdown and pause work in every preset.

## Without the air pump

The mixing states never command the air pump (also in Haupt: it runs 24/7 on its own). Without
the Air Pump function the tank is not aerated, so PH_MIXING and EC_MIXING wait longer:
`mixing time × no_air_pump_settle_factor` (YAML, default 2.0, placeholder), capped at 20 min
(never shorter than the normal time). Haupt (air pump on) is unchanged. The "Air Pump Cycling"
switch only stores the mode in the gate; it applies once a preset with the air pump is active.

## Switching (`requestPreset()`)

Allowed only in **IDLE, NIGHT, SHUTDOWN, PAUSE, ERROR**; in any other state (INIT and every
operation state) it is refused with `operation running (<STATE>)` (no queueing). The same rule
applies to editing the volumes and functions and to "Restart Grow (Day 1)". Switching to the
active preset is a no-op. The state itself does not change (a switch in PAUSE stays in PAUSE).

On a switch, in this order:
1. The new index is written to NVS (`preset_active`) before anything else.
2. HAL volumes, gate blocks and the active calendar follow the new preset.
3. Actuators: nutrient pumps, acid pump, water valve and wastewater pump OFF (only those that are
   on; running actuator tests end). Air pump and grow light OFF only if the new preset lacks them;
   if it has them they stay as they are (no Shelly toggling), and an air pump that the old preset
   lacked gets its stored mode (normal / cycling) back at once.
4. Per-tank state of the old preset is reset: queued post-fill EC and pH checks
   (`auto_ec_check_pending_`, `auto_ph_correction_pending_`), post-fill/post-feed stabilize flags,
   the pH cycle (`resetPhCycle()`) and sensor retry state, the EC cycle (pre-feeding EC, mL/L total,
   attempts, doses, manual mode, mother-mix and reservoir-change flags), the 12 h EC feeding guard
   (`last_ec_feeding_timestamp_`), the pH/EC plausibility history (no jump alert across tanks) and
   the cached auto-feed day / reservoir week (reloaded from the new preset's keys).
   Not reset: the pH check slot schedule (clock-based), the K-factors (global), the auto switches.
5. The new calendar catches up by date at once (`sync_to_date`), and the grow light is
   re-evaluated in the same loop.
6. One INFO line: `Preset Haupt -> Mutter: tank 10.0 / 15.0 L, functions ..., day N`.

Per-day markers are per preset: `AutoFeedDay` / `AutoResWeek` (Haupt, the old keys),
`AutoFeedDay1` / `AutoResWeek1` (Mutter), `AutoFeedDay0` / `AutoResWeek0` (Anzucht). A feed in
Haupt does not block Mutter's feed the same day.

## Boot order and recovery

All components involved run at the default setup priority and are set up in registration order:
HAL → **controller** (`setupPresets()`: store loaded, volumes, gate blocks, calendar index) →
safety gate → PSM → calendar (loads the day slots). INIT, the PSM recovery and the first calendar
date sync all run later from `loop()`, so they already see the active preset.

PSM recovery cannot resume an operation under a different preset: a switch is only possible when
no operation runs, and the index is persisted before anything else. Recovery itself only closes
the valve / stops the drain pump (OFF commands, safe in every preset) or restores SHUTDOWN/PAUSE;
other events only raise an alert. The PSM record is therefore unchanged (no preset field).

## NVS

| Key | Content |
|---|---|
| `preset_active` | `{uint16 version=1, uint8 index, uint8 0}` |
| `preset_slot_0/1/2` | `{float delta_l, float total_l, uint16 version=1, uint8 mask, uint8 0}`, written only when a value changes; an invalid record is ignored (YAML defaults) |
| `calendar_slot_0/1/2` | grow day + sync date per calendar (calendar_manager) |
| `AutoFeedDay[0/1]`, `AutoResWeek[0/1]` | per-preset feed day / reservoir week (see above) |

Once a preset was edited in the UI its record holds all of its values, and later YAML default
changes no longer apply to it.

---

# Air Pump Control System

## Overview

The air pump control system operates in both IDLE and NIGHT states, providing continuous oxygenation to the hydroponic system. It supports two operational modes controlled via web UI.

## Operational Modes

### Mode 1: Normal Mode (DEFAULT - 24/7)
**Status**: Cycling switch OFF
**Behavior**:
- Air pump runs continuously 24 hours per day
- Health check runs every 30 seconds in IDLE and NIGHT states
- Health check forces pump ON using `forceExecute=true` to bypass debouncing
- Automatically corrects manual shutoffs via Shelly web interface
- Handles network failures and dropped HTTP commands

**Correction 2026-11-15**: the code below is history. Today the HAL's Shelly poll (every 30 s,
`hal.cpp`, any controller state) turns the air pump back on through the gate
(`enableCycling("AirPump", false)`) when the Shelly reports it OFF, cycling is off and the active
preset has the Air Pump function.

**Implementation (old)**:
```cpp
// In handleIdle() and handleNight()
if (!safety_gate_->isCyclingEnabled(AIR_PUMP)) {
    // Normal mode (24/7): force pump ON every 30s
    ESP_LOGD(TAG, "[AIR PUMP HEALTH] Normal mode - forcing pump ON");
    requestPump(AIR_PUMP, true, 0, true);  // forceExecute=true, unlimited duration
}
```

**Code Location**: `controller.cpp:532-536` (IDLE), `controller.cpp:612-616` (NIGHT)

### Mode 2: Cycling Mode
**Status**: Cycling switch ON
**Behavior**:
- Air pump cycles between ON and OFF periods
- Periods configured via web UI sliders (10s - 1 hour range)
- Defaults: 120s ON, 60s OFF
- Health check verifies cycling state every 30 seconds
- Uses `forceExecute=true` to re-send cycling commands

**Implementation**:
```cpp
// In handleIdle() and handleNight()
if (safety_gate_->isCyclingEnabled(AIR_PUMP)) {
    // Cycling Mode: Get expected state and force re-send
    bool expected_state = safety_gate_->getState(AIR_PUMP);
    requestPump(AIR_PUMP, expected_state, expected_state ? 600 : 0, true);  // forceExecute=true
}
```

**Code Location**: `controller.cpp:515-531` (IDLE), `controller.cpp:599-611` (NIGHT)

## Force Execute Mechanism

### Purpose
Bypass debouncing to allow health monitoring to re-send commands even when pump is already in the requested state.

### How It Works
1. **Normal Operation**: Safety gate rejects redundant commands (debouncing)
2. **With Force Execute**: Debouncing check is bypassed
3. **Use Case**: Health monitoring can re-confirm pump state every 30s

### Implementation
- **ActuatorSafetyGate**: Modified to check `forceExecute` flag before debouncing rejection
- **Controller**: Uses `forceExecute=true` for health check commands only
- **Code Location**:
  - `ActuatorSafetyGate.cpp:62` (debouncing check)
  - `ActuatorSafetyGate.cpp:68-71` (force execute logging)
  - `controller.cpp:2383` (requestPump with forceExecute parameter)

### Safety Note
Force execute only bypasses debouncing, NOT duration limits or other safety checks. It's reserved for system health monitoring only.

## Health Check Behavior

**Interval**: Every 30 seconds (AIR_PUMP_HEALTH_CHECK_INTERVAL constant)

**States**: IDLE and NIGHT

**Actions**:
- Query cycling status from ActuatorSafetyGate
- Determine expected pump state (ON for normal mode, cycling state for cycling mode)
- Send command with `forceExecute=true` to ensure Shelly receives it
- Log health check actions at DEBUG level

**Reliability Benefits**:
- Detects and corrects manual shutoffs via Shelly web interface
- Recovers from network timeouts and dropped HTTP commands
- Ensures continuous oxygenation even with WiFi issues
- Works identically in both IDLE and NIGHT states

## Configuration

### Web UI Controls (Section 01)
1. **Switch**: "01_04_Air Pump Cycling"
   - OFF (default): Normal mode - 24/7 operation
   - ON: Cycling mode - automatic ON/OFF cycling

2. **Number Sliders**: "01_05_Air Pump ON Period (seconds)" and "01_06_Air Pump OFF Period (seconds)"
   - Range: 10 - 3600 seconds (10s - 1 hour)
   - Defaults: 120s ON, 60s OFF
   - Only used when cycling mode is enabled

### Boot Configuration (`actuator_safety_gate:` block in plantOS.yaml)
```yaml
max_durations:
  AirPump: 0s           # 0 = unlimited (24/7 operation)
air_pump_cycling:       # applied in ActuatorSafetyGate::setup(): periods set, cycling OFF (Normal mode 24/7)
  on_period: 120s
  off_period: 60s
```

## State Entry Initialization

When transitioning TO IDLE or NIGHT states (future enhancement):
- Check if cycling is enabled
- Get expected cycling state
- Send initial command to start pump in correct state
- Prevents stale state information from previous states

---

# Implementation Guidelines

## When Modifying the FSM

**CRITICAL**: When making changes to the FSM, you **MUST** update this document (`FSMINFO.md`) to reflect:

1. **New States**: Add state to diagram, actuator table, PSM table, timeout table
2. **Modified Transitions**: Update diagram arrows and triggers
3. **Changed Timeouts**: Update timeout table with new values
4. **New Actuator Actions**: Update actuator actions table
5. **New Safety Features**: Document in safety features section
6. **New Public API Methods**: Add to external triggers table

## Code Locations

- **State Enum**: `components/plantos_controller/controller.h:46-62` (ControllerState enum)
- **State Handlers**: `components/plantos_controller/controller.cpp` (handleInit, handleIdle, etc.)
- **Public API**: `components/plantos_controller/controller.h:149-218` (start* methods, setTo* methods)
- **State Dispatch**: `components/plantos_controller/controller.cpp:214-274` (loop() switch statement)
- **Transitions**: Each handler calls `transitionTo(ControllerState)` to change states

## Testing Changes

After modifying the FSM:
1. Build and flash firmware: `task run`
2. Verify LED behavior matches state description
3. Test all transitions into and out of modified states
4. Verify PSM recovery if state uses persistence
5. Check actuator behavior matches table
6. Test error conditions and SafetyGate rejections

---

**Document Version**: 1.1
**Last Updated**: 2026-01-05
**Maintained By**: PlantOS Development Team
