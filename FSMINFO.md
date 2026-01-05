# PlantOS Controller - Finite State Machine (FSM) Documentation

**IMPORTANT**: This file is the **authoritative source** for PlantOS Controller FSM behavior. All implementation changes to FSM states, transitions, triggers, or actuator control **MUST** be reflected in this document.

**Location**: `/home/cody/plantOS-testlab/FSMINFO.md`
**Component**: `components/plantos_controller/controller.cpp` and `controller.h`
**Last Updated**: 2026-01-03

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
     │  - Wait 30s between  │
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
     │  Turn ON WaterValve  │
     │  (max 30s duration)  │
     │                      │
     │  Monitor:            │
     │  - Water level HIGH  │
     │    sensor            │
     │  - Timeout (30s)     │
     └──────────────────────┘
              │
              ├──[SafetyGate rejects]────────────▶IDLE
              │
              ├──[HIGH sensor ON]────────────────┐
              │  Close valve                     │
              │  Clear PSM                       │
              │                                  │
              ├──[Timeout (30s fallback)]────────┤
              │  Close valve                     │
              │  Clear PSM                       │
              │                                  │
              └──────────────────────────────────┼────────────────────┐
                                                 │                    │
                          ┌──────────────────────┼────────────────┐   │
                          │ [auto_ph_          [No auto pH]       │   │
                          │  correction_                          │   │
                          │  pending = true]                      │   │
                          ▼                                       ▼   │
                   ┌────────────┐                              [IDLE] │
                   │     PH_    │                                     │
                   │ PROCESSING │                                     │
                   └────────────┘                                     │
                                                                      │
                                                                      │ [Timeout with
                                                                      │  no sensor]
                                                                      ▼
                                                                   [ERROR]

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
     │  Pump (max 30s)      │
     │                      │
     │  Monitor:            │
     │  - Water level LOW   │
     │    sensor OFF        │
     │  - Timeout (30s)     │
     └──────────────────────┘
              │
              ├──[Tank already empty (LOW OFF)]──▶IDLE
              │
              ├──[SafetyGate rejects]────────────▶IDLE
              │
              ├──[LOW sensor OFF]────────────────┐
              │  Stop pump                       │
              │  Clear PSM                       │
              │                                  │
              ├──[Timeout (30s fallback)]────────┤
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
     │  SAFETY CHECK:       │
     │  Verify tank empty   │
     │  (both sensors OFF)  │
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
| INIT               | - None (LED only: Red→Yellow→Green) |
| IDLE               | - None (LED only: Breathing Green) |
| SHUTDOWN           | - Turn OFF all actuators (emergency safety) |
| PAUSE              | - Maintain current actuator states (no changes) |
| ERROR              | - Turn OFF all pumps (safety) |
| PH_PROCESSING      | - None (read pH sensor only) |
| PH_MEASURING       | - Turn OFF all pumps (for accurate pH reading) |
| PH_CALCULATING     | - None (calculation only) |
| PH_INJECTING       | - Turn ON AcidPump (duration: calculated dose in mL → seconds)<br>- Turn ON AirPump (optional, for mixing during injection) |
| PH_MIXING          | - Turn OFF AcidPump<br>- Keep AirPump ON (2 minutes mixing) |
| PH_CALIBRATING     | - None (sensor calibration only, no actuators) |
| FEEDING            | - Turn ON NutrientPumpA (duration from calendar: dose_A_ml)<br>- Turn ON NutrientPumpB (duration from calendar: dose_B_ml)<br>- Turn ON NutrientPumpC (duration from calendar: dose_C_ml)<br>  (sequential: A → B → C) |
| WATER_FILLING      | - Turn ON WaterValve (max 30s, or until HIGH sensor) |
| WATER_EMPTYING     | - Turn ON WastewaterPump (max 30s, or until LOW sensor OFF) |
| FEED_FILLING       | - Turn ON WaterValve (calc'd duration, or until HIGH sensor) |

## Persistent State Manager (PSM) Events

| STATE              | PSM EVENT LOGGED          | RECOVERY ACTION ON BOOT |
|-------------------|---------------------------|------------------------|
| SHUTDOWN           | "STATE_SHUTDOWN"          | Restore SHUTDOWN state after INIT (persists across reboot) |
| PAUSE              | "STATE_PAUSE"             | Restore PAUSE state after INIT (persists across reboot) |
| PH_MEASURING       | "PH_CORRECTION" (0)       | Operation interrupted, return to IDLE |
| PH_INJECTING       | (inherited from MEASURING)| Operation interrupted, return to IDLE |
| PH_MIXING          | (inherited from MEASURING)| Operation interrupted, return to IDLE |
| FEEDING            | "FEEDING" (0)             | Operation interrupted, return to IDLE |
| WATER_FILLING      | "WATER_FILL" (0)          | Force-close WaterValve, clear PSM |
| WATER_EMPTYING     | "WATER_EMPTYING" (0)      | Force-stop WastewaterPump, clear PSM |
| FEED_FILLING       | "FEED_OPERATION" (0)      | Force-close WaterValve, clear flags & PSM |
| PH_PROCESSING      | (no PSM logging)          | N/A |

## External Triggers (Public API)

### Manual Triggers

| PUBLIC METHOD              | PRECONDITION       | TRANSITION TO        | DESCRIPTION |
|---------------------------|--------------------|----------------------|-------------|
| startPhCorrection()        | Must be in IDLE    | PH_MEASURING         | Manual pH correction (web UI button)<br>**Blocked in NIGHT state** |
| startPhCalibration()       | Must be in IDLE    | PH_CALIBRATING       | 3-point sensor calibration (web UI) |
| startFeeding()             | Must be in IDLE    | FEEDING              | Manual nutrient dosing (web UI button)<br>**Blocked in NIGHT state** |
| startFillTank()            | Must be in IDLE    | WATER_FILLING        | Fill tank + auto pH correction<br>**Blocked in NIGHT state** |
| startEmptyTank()           | N/A (info only)    | IDLE (no change)     | Info message (use manual drain) |
| startFeed()                | Must be in IDLE<br>Tank must be empty | FEED_FILLING         | Complete feed: Fill→Nutrients→pH<br>(safety check: both sensors OFF)<br>**Blocked in NIGHT state** |
| startReservoirChange()     | Must be in IDLE    | IDLE (manual drain)  | Info message (requires manual empty)<br>**Blocked in NIGHT state** |
| setToShutdown()            | Any state          | SHUTDOWN             | Emergency shutdown (persists) |
| setToPause()               | Any state          | PAUSE                | Pause system (persists) |
| setToIdle()                | SHUTDOWN or PAUSE  | IDLE                 | Resume from SHUTDOWN/PAUSE |
| resetToInit()              | Any state          | INIT                 | Reset to boot sequence |

### Automatic Triggers

| AUTOMATIC TRIGGER         | PRECONDITION       | TRANSITION TO        | DESCRIPTION |
|---------------------------|--------------------|----------------------|-------------|
| Every 2 hours (in IDLE)    | System in IDLE     | PH_PROCESSING        | Periodic pH monitoring (**NOT triggered in NIGHT**) |
| Night mode hours start     | IDLE + night mode enabled + current hour in range | NIGHT | Automatic transition to night mode |
| Night mode hours end       | NIGHT + (night mode disabled OR hour out of range) | IDLE | Automatic transition back to idle |
| PSM recovery check         | Boot from INIT     | SHUTDOWN or PAUSE    | Restore persisted state after power loss |
| Time-based grow light      | Calendar + Time    | (no state change)    | Control GrowLight via HAL based on schedule |

## State Timeout Values

| STATE              | TIMEOUT/DURATION           | ACTION AFTER TIMEOUT |
|-------------------|---------------------------|---------------------|
| INIT               | 3 seconds                  | → IDLE (or SHUTDOWN/PAUSE if PSM recovery) |
| IDLE               | No timeout                 | Continuous (automatic → NIGHT if night mode hours) |
| NIGHT              | No timeout                 | Continuous (automatic → IDLE when hours end) |
| ERROR              | 5 seconds                  | → INIT (automatic recovery) |
| PH_MEASURING       | 5 minutes (300s)           | → PH_CALCULATING |
| PH_INJECTING       | Calculated dose + 200ms    | → PH_MIXING |
| PH_MIXING          | 2 minutes (120s)           | → PH_MEASURING (loop back to verify) |
| WATER_FILLING      | 30s (fallback) or sensor   | → IDLE or PH_PROCESSING (if auto pH) |
| WATER_EMPTYING     | 30s (fallback) or sensor   | → IDLE |
| FEED_FILLING       | Calculated or sensor       | → FEEDING |

---

# Safety Features in FSM

## 1. ActuatorSafetyGate Integration (Layer 2)
All actuator commands flow through ActuatorSafetyGate which provides:
- **Debouncing**: Prevents redundant commands
- **Duration limits**: Enforces max runtime per actuator
- **Soft-start/soft-stop**: PWM ramping to protect hardware

## 2. Critical pH Monitoring
- If pH < 5.0 or pH > 7.5: log to PSM, add alert, continue measuring
- pH range checked in PH_CALCULATING before correction
- Max 5 correction attempts before aborting to IDLE

## 3. Water Level Sensor Monitoring
- **WATER_FILLING**: Abort on HIGH sensor (prevent overflow)
- **WATER_EMPTYING**: Abort on LOW sensor OFF (prevent dry pump)
- **FEED_FILLING**: Pre-check both sensors OFF (tank must be empty)

## 4. Persistent State Recovery (PSM)
- SHUTDOWN and PAUSE states persist across power loss
- Water operations force-close valves/pumps on boot recovery
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
- **Default Configuration**: Disabled (off), 22:00 start, 08:00 end when enabled
- **Web UI Controls**:
  - Switch: "Night Mode Enabled" (toggle on/off)
  - Number: "Night Mode Start Hour" (0-23, default: 22)
  - Number: "Night Mode End Hour" (0-23, default: 8)

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

**Implementation**:
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

### Boot Configuration (plantOS.yaml:22-28)
```yaml
id(actuator_safety)->setMaxDuration("AirPump", 0);            // 0 = unlimited (24/7 operation)
id(actuator_safety)->setCyclingPeriods("AirPump", 120, 60);  // Default: 2 min ON, 1 min OFF
id(actuator_safety)->enableCycling("AirPump", false);        // Default: Normal mode (24/7)
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
