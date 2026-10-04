# Web UI Performance Cleanup – Final Plan

Finalized 2026-10-04 with the user. Base commit `d3621df`. Line numbers point into
`plantOS.yaml` unless a file is named. The goal is performance **without losing
flexibility**: features get switched off (runtime toggle or build-time package), not deleted.

The system is not in grow mode right now. Keep changes simple. Each step gets a
`task build`, then a commit and push to `main`. The user decides when to flash.

---

## 0. Symptom and baseline

The web UI at http://192.168.0.201/ loads slowly and shows only sensors and text sensors.
Buttons, switches and numbers are missing.

Baseline measured 2026-10-04 (`curl -N /events` for 10 s, one client):

| Metric | Value |
|---|---|
| SSE `state` events total | 394 |
| of which `light-built-in_led` | **260 (~26/s)** |
| `text_sensor-uptime` (1 s interval) | 6 in 10 s → the main loop is lagging |
| `log` events | 57 |
| Distinct entities in the initial sync | 112 (all of them, including 38 buttons, 37 switches and 13 numbers) |

Conclusion: the device does send all entities. The browser frontend drowns in LED updates
while it builds the page, and the loop is slowed by LED publishing (SSE, MQTT/TLS and API on
every `perform()`) and by synchronous Shelly HTTP.

---

## 1. Decisions

| Topic | Decision |
|---|---|
| LED | Throttle in the HAL and make the light `internal: true`. Add an LED test button to the debug package. |
| Logs | Keep the web log, but make it lean. The full reports become on-demand buttons. |
| Debug entities | Move them into the build-time package `packages/debug.yaml`. |
| Shelly | Async worker task in `ESPHomeHAL` plus a **runtime** switch "Shelly Integration" (persisted, default ON). |
| Shelly OFF → WastewaterPump | WATER_EMPTYING and reservoir change are **refused**. |
| MQTT | Stays (feeds Grafana via `plantos/vitals`). Revisit only after measuring. |
| ESPHome upgrade | Out of scope. |

Flexibility rule: anything that **costs while running** gets a runtime toggle. Anything that
**costs by existing** (entity count) goes into a build-time package.

---

## 2. Steps

### Step 1 – LED throttle (HAL) and internal light
- `ESPHomeHAL::setSystemLED` (`hal.cpp` ~1025): skip `perform()` unless the value changes
  visibly (Δ > ~2 % per channel or brightness, or the on/off state flips), and cap at 20 Hz.
  A change of on/off state always goes through.
- `light: system_led` → `internal: true` (no web, MQTT or API publishing).
- **Done when:** a 10 s `/events` capture shows no `light-` events.

### Step 2 – Log diet
- Logger compile level `VERBOSE` → `DEBUG`. `wifi` and `http_request` → `WARN`.
  The runtime "Verbose: …" switches now raise tags to DEBUG (the compile-time maximum).
- Controller status report: the existing "System Status Reports" switch is now `ALWAYS_OFF`.
  The new button "Print Status Report" (`sg_system`) calls
  `PlantOSController::requestStatusReport()` → `logStatus(force=true)`. This is simpler than
  writing a second, compact report; the web UI already shows the same data as entities.
  The DS18B20 pin label is fixed (GPIO3).
- GPIO status interval: replaced by the button "Log GPIO Levels" (`sg_debug_gpio`) with the
  correct pinout.
- `calendar_manager` status log: periodic interval → 10 min.
- **Done when:** no burst of more than ~10 lines per minute from periodic reports.

### Step 3 – Debug package
- Enable `packages:` in `plantOS.yaml`. Move all `sg_debug_*` entities, the Shelly test
  buttons and the LED test button into `packages/debug.yaml`. Keep the sorting groups in
  core so the package doesn't need to redefine them.
- Production build: the include line is commented out. Debug build: uncomment one line.
- Don't move anything that `on_boot` or other core lambdas reference by `id()`
  (`pwm_gpio*_slider`, `ph_k_factor_slider`, Shelly switches used by the HAL).
- **Done when:** both variants (with and without the package) pass `task build`.

### Step 4 – Async Shelly worker and runtime toggle
- Inside `ESPHomeHAL` only (3-layer rule). A FreeRTOS task uses `esp_http_client` directly.
  The ESPHome `http_request` component is not used from the task.
  - Command queue (socket, on/off, or a pattern URL) → the worker sends it (the existing
    retry and backoff logic moves into the worker).
  - Poll every 30 s → a cached result (reachable, uptime, switch states) behind a mutex or
    a flag.
  - `ESPHomeHAL::loop()` takes the results and runs the existing ASG/state sync on the
    main thread. No logging from the task except through flags.
  - Timeout 2 s (LAN).
- Runtime switch **"Shelly Integration"** (`sg_controller_settings`, `RESTORE_DEFAULT_ON`).
  The HAL gets `setShellyEnabled(bool)` and `isShellyEnabled()`.
  - OFF: the worker is idle, commands are dropped with **one** WARN, and status shows
    "disabled" (no alert storm).
  - Controller: WATER_EMPTYING and reservoir change are refused when Shelly is disabled
    (same pattern as `0cbdf5a`). AirPump mixing just waits out its time. GrowLight and
    night mode have no effect.
- Update `FSMINFO.md` (new refusal condition).
- The YAML interval `pingShellyDevice` is removed; the worker polls on its own.
- **Done when:** the build passes, and with Shelly off or unplugged the loop never blocks
  (uptime ticks 1/s on SSE).

### Step 5 – Review MQTT (only if needed)
- After flashing steps 1–4, measure again. If MQTT is still noticeable, check whether
  ESPHome 2025.4 supports per-entity MQTT opt-out. Otherwise leave it.

---

## 3. Constraints
- HAL → ASG → Controller. No new standalone components, no actuator calls in YAML lambdas.
- Non-blocking loop. No `ESP_LOG*` from task or ISR context.
- Template switch lambdas return cached values.
- Every new entity gets a `sorting_group_id`.
