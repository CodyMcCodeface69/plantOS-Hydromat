# Host unit tests

Unit tests for the pure logic of the firmware, run on the laptop with the system `g++`.
They compile the **real** sources from `components/` (unmodified) against small stub headers,
so they describe what the firmware does today. They exist to check the controller refactor
and the calendar rewrite without hardware.

## Run

```bash
task test                 # build + run everything (~15 s clean, ~1 s incremental)
task test -- calendar     # only tests whose "group.name" contains "calendar"

cd tests
make                      # same as task test
make run ARGS=ec_dose
PLANTOS_TEST_VERBOSE=1 make          # list every passing test
PLANTOS_TEST_LOG=1 make run ARGS=acid_dose   # show the firmware's ESP_LOGx output
./build/plantos_tests --list
make clean
```

Needs `g++` (C++17), `make`, `python3`. Nothing is downloaded. ArduinoJson (header-only) is
copied once from `.esphome/build/*/.piolibdeps/*/ArduinoJson` into `tests/build/vendor/`; if
that tree does not exist yet, run `task build` once or pass `ARDUINOJSON_DIR=<.../ArduinoJson/src>`.
All build output goes to `tests/build/` (git-ignored).

## What is covered

| File | Real code under test |
|---|---|
| `test_calendar.cpp` | `CalendarManager`: JSON parse of the schedule from `plantOS.yaml`, lookup, defaults, mS→µS conversion, day counter + NVS, `sync_to_date` |
| `test_ph_math.cpp` | `calculateAcidDoseML`, `updatePhKFactor`, `calculatePhMixingDuration`, `calculateRobustPhAverage`, `isPhStable`, `handlePhProcessing`, `handlePhCalculating` |
| `test_ec_math.cpp` | `updateEcKFactor`, `handleEcProcessing`, `handleEcCalculating`, `handleEcFeeding`, `handleEcMeasuring`, `handleFeeding`, `startMotherNutrients` |
| `test_time_logic.cpp` | `isNightModeHours`, `getLocalDateOrdinal`, `getCurrentGrowDay`, auto-feed and auto-reservoir triggers, the pH slot schedule and date sync in `handleIdle` |
| `test_pure_math.cpp` | Free functions in `plantos_controller/dosing_math.*` and `schedule_math.*`, called directly: grow-light window (incl. wrap past midnight), night-hour window, `daysFromCivil`, grow day, week/weekday, pH check slots, acid dose, K-factor updates, mixing time, robust average, EC window/doses |
| `test_robust_averager.cpp` | `sensor_filter/RobustAverager.h` |
| `test_safety_gate.cpp` | `ActuatorSafetyGate`: debounce, max duration, watchdog, interlock, ramping, cycling |

`// SUSPECT:` comments mark behavior that looks like a bug. The test asserts the current
behavior anyway. When such a bug gets fixed on purpose, change the expectation in the same commit.

### Golden schedule

`golden/haupt_schedule.csv` holds all 120 days as the firmware sees them after parsing
(EC in µS/cm). `calendar.golden_haupt_schedule_all_120_days` compares the real parser output
with it, and `tools/check_golden.py` compares it with the JSON in `plantOS.yaml` independently
of the C++ parser. A calendar rewrite must reproduce these rows.

If the schedule is changed on purpose: `make golden`, then review the diff of the CSV.

## How it works

- **Stubs** (`stubs/`): minimal stand-ins for the ESPHome / ESP-IDF headers that the components
  include (`esphome/core/{component,hal,log,helpers,preferences,time}.h`,
  `esphome/components/time/real_time_clock.h`, `freertos/*.h`). `-Istubs` comes first on the
  include path; `build/inc/esphome/components` is a symlink to the repo's `components/`, so
  `#include "esphome/components/plantos_hal/hal.h"` resolves to the real header.
- **Fake time and NVS** (`stubs/host_stubs.cpp`): `esphome::millis()` returns a counter that
  tests move with `host::set_millis()` / `host::advance_millis()`. libc `time()` is replaced and
  set with `host::set_unix_time()`. `global_preferences` is an in-memory map; "reboot" = create
  new objects without calling `host::reset()`. The runner calls `host::reset()` before each test.
- **Fakes** (`support/`): `FakeHAL` implements the abstract `plantos_hal::HAL` with public fields
  for sensor values and a list of recorded actuator calls. `ControllerFixture` wires the real
  controller, safety gate, PSM and calendar (production schedule) to it, plus a settable clock.
- **Private access**: the tests are compiled with `-fno-access-control`, so they call private
  methods (`f.ctrl.calculateAcidDoseML(...)`) and set private members (`f.ctrl.ph_K_ = 0.07f`)
  directly. No `friend` declarations or edits in `components/` are needed.
- **Schedule**: `tools/extract_schedule.py` pulls `schedule_json` out of `plantOS.yaml` into
  `build/schedule.json` on every run; tests read it through `production_schedule_json()`.
- **Framework**: `support/minitest.h` (`TEST`, `CHECK`, `CHECK_EQ`, `CHECK_NEAR`, `REQUIRE`).

Not compiled: `plantos_hal/hal.cpp` (ESP-IDF HTTP, FreeRTOS), the sensor components and
`sensor_filter.cpp`. Only their headers are used where needed.

Limits to keep in mind: the host is 64-bit x86 and the device a 32-bit RISC-V; `float` math is
the same IEEE single precision, but ArduinoJson's memory pool sizes differ, so "JSON too big for
the pool" cannot be tested here. Function-local `static` variables in the controller
(`handleIdle`, `handleEcFeeding`, `loop`) keep their value between tests.

## Add a test

1. Pick the file by topic, or create `test_<topic>.cpp` (every `*.cpp` in this directory is built).
2. Write:

   ```cpp
   #include "minitest.h"
   #include "fixture.h"

   TEST(acid_dose, my_new_case) {
     ControllerFixture f;            // real controller + FakeHAL + PSM + calendar + clock
     f.hal.tank_volume_delta = 20.0f;
     f.ctrl.ph_K_ = 0.1f;
     CHECK_NEAR(f.ctrl.calculateAcidDoseML(6.85f, 6.0f), 2.0f, 1e-3);
   }
   ```

3. Write expected values as numbers worked out by hand (with the arithmetic in a comment), not
   by repeating the firmware's formula in the test.
4. A new component source goes into `COMPONENT_SRCS` in the `Makefile`. If it includes an
   ESPHome header that has no stub yet, add the smallest stub that compiles under `stubs/`.
5. To drive a state handler: `f.force_state(ControllerState::X)`, call `f.ctrl.handleX()`,
   advance time with `host::advance_millis()`, check `f.ctrl.getCurrentState()` and `f.hal.calls`.
