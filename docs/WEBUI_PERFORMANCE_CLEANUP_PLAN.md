# Web UI Performance Cleanup – Handoff Plan (draft 1)

Written 2026-10-04 as a briefing for the next agent. **CLAUDE.md (auto-loaded) plus this
file is all you need. Skip the other docs.** Everything below was checked against the code at commit `8830ccf`.
Line numbers point into `plantOS.yaml` unless a file is named.

---

## 0. Symptom (from `plantos.pdf`, a print of http://192.168.0.201/)

- The page loads slowly and only part of it renders. It shows the **sensor and
  text_sensor entities** (Status, Sensors, Calendar day, HAL mirrors), one number
  (pH K-Factor), the Built-in LED light and the Debug Log.
- **Missing entirely:** all buttons (38), almost all switches (37) and numbers (13).
  The "Controller" and "Actuators" groups, plus most of "Controller Settings",
  "Debug: Logging" and "Debug: GPIO Bypass & PWM", never appear.
- Device uptime was 168 h, so this is not a boot-time effect.

Web UI v3 builds the entity list from SSE `state` events that it receives after
connecting. If entities are missing, the device dropped or never sent those events,
because the SSE channel or the main loop was saturated.

---

## 1. What to read (and what to skip)

| Doc | Status |
|---|---|
| `CLAUDE.md` (a symlink to `AGENTS.md`) | **Up to date** as of 2026-10-04. It's short and covers pins, actuators, the component inventory, the YAML layout, commands and the HAL rules. It is loaded automatically, so you don't need to re-read it. |
| This file | Everything specific to web performance. |
| `docs/PERFORMANCE_ANALYSIS.md`, `docs/LED_OPTIMIZATION_ANALYSIS.md` | **Skip.** They're from Dec 2025 and conclude the LED loop is "cheap". That's wrong for this problem, because they only measured CPU time and ignored the publish side effects (see H1). |
| `FSMINFO.md` | Only needed if FSM states change. This cleanup should not change them. |
| `TODO.md`, other `docs/*` | **Skip.** Not related to web performance. |

The rules that matter are already in CLAUDE.md:
- Hardware goes through HAL → ASG → Controller.
- The user commits; give them a commit message and don't commit yourself.
- ESPHome is 2025.4.2. `task build` takes ~70 s and `task snoop` attaches to the logs.

---

## 2. Code map (performance-relevant details only)

The full YAML section map and component inventory are in CLAUDE.md. This section adds
what matters for web performance.

```
plantOS.yaml (3422 lines)
  8    esphome: on_boot lambda (ASG durations, PWM sync, K-factor callback)
  64   logger:  level VERBOSE (compile-time!), wifi/http_request VERBOSE
  183  web_server: v3, 10 sorting_groups, no local/js override (www/ unused)
  231  api:     enabled
  247  mqtt:    TLS to Hetzner, discovery:false (still publishes EVERY entity state!)
  264  http_request: used synchronously by HAL for Shelly
  345  light:   system_led (WS2812, RMT), visible in web UI (sg_system)
  436  sensor:  6 (ADC tds_adc_raw at 100ms internal, EC 5s, pH, DS18B20, BME280)
  599  binary_sensor: 3 water level GPIO
  683  text_sensor: 12 template (2 at 1s: Uptime, Current Time; rest 5–60s)
  893  button: 38  (lines 893–1669, ~780 lines)
  1775 plantos_controller.status_report_interval: 60s
  1938 calendar_manager.status_log_interval: 30s
  2069 switch: 37  (lines 2069–3016, ~950 lines, mostly template + lambdas)
  3017 number: 13
  3306 interval: GPIO debug log (30s), Shelly poll (30s), MQTT vitals (30s)
  3383 script: wifi reconnect helpers
Entity total ≈ 112 visible (groups: settings 25, actuators 20, debug_logging 15,
controller_actions 14, sensors 11, debug_gpio 9, status 5, debug_hal 5, system 5, calendar 3)

components/ (≈18k LOC C++)
  plantos_controller/controller.cpp   4816  loop() @130, status report build @150–284
  plantos_controller/CentralStatusLogger.cpp 806  logStatus(): ~60 log lines per report
  plantos_controller/{breathing_green,*_pulse,...}.cpp  LED behaviors
  plantos_hal/hal.cpp                 1373  loop() @189, setSystemLED @1025,
                                            pingShellyDevice @1098 (sync HTTP GET)
  actuator_safety_gate/ActuatorSafetyGate.cpp 940  loop() @312
  ezo_ph_uart/ezo_ph_uart.cpp          867  read_response_ @619 busy-waits with delay(1)
```

---

## 3. Hypotheses, ranked by likely impact

### H1. LED animation floods every state channel (very likely the main cause)
- In IDLE, `breathing_green.cpp:20` calls `hal->setSystemLED(...)` **on every loop
  iteration**, and `hal.cpp:1025–1045` runs `led_->make_call()...perform()` each time.
- Every `perform()` publishes a new light state, which then goes to:
  - **web_server SSE** (the light is a visible entity in `sg_system`). That means
    hundreds to thousands of `light` state events per second per browser client.
    The initial entity list for buttons, switches and numbers gets queued behind
    them or dropped.
  - **MQTT**: `mqtt:` creates a state topic for *every* entity even with
    `discovery: false`, so this is a TLS publish storm.
  - **API**, if Home Assistant is connected.
- Fix options (pick one):
  - **a)** Make the light `internal: true` (no web, MQTT or API publishing). This is the cheapest fix.
  - **b)** Throttle in the HAL: only call `perform()` when the color or brightness
    changes by more than a threshold, and cap updates at ~30–50 Hz. Keep this in
    `ESPHomeHAL::setSystemLED`.
  - **c)** Bypass LightState and drive the `esp32_rmt_led_strip` output directly from
    the HAL. This is the most work, but it removes publishing completely.
  - The recommended choice is **a + b**. The web UI LED control is debug-only anyway.

### H2. Log streaming over SSE is heavy
- `logger.level: VERBOSE` is the *compile* level, so every VERBOSE/DEBUG string is
  compiled in. `wifi` and `http_request` are set to VERBOSE at runtime.
- `CentralStatusLogger::logStatus()` emits **~60 lines in one burst** every 60 s
  (`status_report_interval`, line ~1775). The lines include ANSI escapes, box art
  and `****` banners. Each line is a separate SSE `log` event to every client.
- The interval at line 3307 logs a 20-line GPIO report every 30 s at DEBUG. Its pin
  names are also stale: it lists GPIO16–23 as UART/temp/level, which doesn't match
  the current pinout in CLAUDE.md.
- `calendar_manager` logs its own status every 30 s (`status_log_interval`, line ~1938).
  That's another periodic burst; consider 10 min or verbose-only.
- `controller.cpp` (~line 200, status report build) hard-codes "DS18B20 ... GPIO23".
  The real pin is GPIO3. Fix it while touching the report.
- Options:
  - **a)** Set the compile level to `DEBUG` or `INFO`, and drop `wifi`/`http_request` to WARN.
  - **b)** Compact the status report to ≤10 lines, or log it at DEBUG only when
    verbose mode is on. The web UI already shows the same data as entities.
  - **c)** Delete the GPIO interval, or gate it behind the verbose switch.
  - **d)** Set `web_server: log: false` and rely on `task snoop`/MQTT. This is the
    biggest win, but the user loses the in-browser log, so ask the user first.

### H3. Synchronous Shelly HTTP blocks the main loop
- `hal.cpp:228, 498, 586, 1131` call `http_request_->get(...)` **synchronously**
  from `loop()` and the intervals. The timeout is 5 s (line 264). In ESPHome 2025.4,
  web_server SSE sends and deferred queues run from the main loop, so while a GET
  is in progress the UI stalls. When the Shelly is slow or unreachable, that stall
  lasts up to 5 s, plus retries (`hal.cpp:203–250`).
- Options:
  - **a)** Run the Shelly I/O in a FreeRTOS task with a queue, and have the HAL read
    cached results. This fits the existing HAL boundary.
  - **b)** Use the ESPHome `http_request` action, which is async in newer versions,
    with `on_response`.
  - **c)** Minimum fix: lower the timeout to ~1.5 s for LAN and poll less often.

### H4. Entity count and YAML size (structural cleanup)
- About 112 entities, roughly 30 of them debug-only (`sg_debug_*`: 29). Each one costs
  RAM, the SSE initial sync, MQTT topics and API list time.
- Many buttons and switches are test or debug actions (Shelly test buttons
  @1565–1620, GPIO bypass, HAL mirrors, per-component verbose switches).
- Options:
  - **a)** Mark debug entities `internal: true`, or remove them (HAL mirrors duplicate the real sensors).
  - **b)** Move debug entities into a `packages:` file (`debug.yaml`) that is only
    included in a debug build.
  - **c)** Collapse the 15 "Debug: Logging" switches into one `select` (log profile).
  - **d)** Split `plantOS.yaml` into `packages/` (core, sensors, actuators, ui,
    debug). This helps maintainability, not runtime.

### H5. MQTT publishes all entities
- `mqtt:` without `discovery` still creates a state topic per entity, so ~112 topics
  over TLS. Only `plantos/vitals` (interval at 3363) is actually consumed.
- Options:
  - **a)** Keep MQTT and turn off MQTT per entity. It's tedious; check which
    per-entity option 2025.4 supports.
  - **b)** Drop the `mqtt:` component, which removes per-entity publishing, and
    send vitals through `http_request` POST or a minimal custom publisher.
  - TLS on the C6 also costs ~40 KB of heap, which is another reason to consider (b).

### H6. Minor or secondary
- `ezo_ph_uart.cpp:619–690`: `read_response_` busy-waits with `delay(1)` up to a
  timeout. Check which callers use it. It's probably only calibration or commands;
  if `update()` uses it, it blocks the loop for each reading.
- `ezo_ph_uart.cpp:516–591`: several `delay(50)` calls in command paths.
- 1 s template text sensors (Uptime, Current Time) cost 2 SSE events/s. That's fine
  once H1 is fixed; 5 s would also be acceptable.
- `tds_adc_raw` at 100 ms is internal, so it's cheap. Leave it.
- **ESPHome upgrade:** 2025.4.2 is old. Later releases reworked web_server (SSE
  deferral and dedup, lower heap use), the scheduler and the logger. The upgrade is
  worth considering, but it touches the Nix flake and the leon-v ADC fork
  (`external_components`, line ~300), which may no longer be needed on newer
  ESPHome for the C6 ADC.
- Unused components (`central_status_logger/`, `ezo_ph`, `time_dummy`, `psm_checker`,
  `dummy_actuator_trigger`, `sensor_dummy`) don't affect runtime. Only `sensor_dummy`
  is listed in `external_components`. Deleting them is optional repo hygiene and
  makes no runtime difference; see CLAUDE.md "Component inventory".
- PSM shows a stale `PH_CORRECTION` event that is 6.6M seconds old. This is
  cosmetic, but `wasInterrupted()` logic may log every report.

---

## 4. Suggested order of work

1. **Measure first** (cheap):
   - In the browser devtools Network tab, open `/events` and watch the rate of
     `state` events for `light-built-in_led`. If it is ≫10/s, H1 is confirmed.
   - Add a loop-time readout: ESPHome `debug:` component, which provides
     `loop_time` and `free heap` sensors, or log `App.get_loop_interval`. Remove it
     after measuring.
   - `task snoop` for 2 min: look for `component took a long time` warnings. Note
     that `component: ERROR` hides them, so set `component: WARN` temporarily.
2. **H1** (LED internal plus HAL throttle). It's 1 YAML line plus ~15 lines in
   `hal.cpp`. Re-test the UI; this alone probably fixes the missing entities.
3. **H2a/b/c** (logger compile level, status report diet, GPIO interval).
4. **H3** (Shelly off the main loop). This is the biggest code change and stays inside the HAL.
5. **H4/H5** (entity diet, MQTT). Ask the user which debug entities they still use.
6. Optional: ESPHome upgrade as a separate step.

After each step: `task build`, flash, reload the UI, and confirm that all groups
render and that first paint takes under ~3 s.

---

## 5. Constraints and gotchas

- Keep the 3-layer rule: LED and Shelly fixes go in `ESPHomeHAL`, not in YAML lambdas
  or new standalone components.
- `on_boot` (line ~15) reads `pwm_gpio*_slider` and `ph_k_factor_slider`. Don't make
  those `internal`, because they're user settings.
- Template switches read controller state via lambdas
  (`isAutoFeedingEnabled()` etc.). The comment says not to call `loadState()` in
  lambdas because it allocates. Keep them cached.
- If the light becomes `internal`, the web UI loses the LED debug control. That's
  acceptable, but mention it to the user.
- Update `FSMINFO.md` only if controller states or transitions change. This plan
  shouldn't need that.
