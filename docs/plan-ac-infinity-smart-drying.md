# Smart Drying Mode via AC Infinity Controller 69

## Context

The user wants PlantOS to drive an **AC Infinity Controller 69** fan to run a
**smart drying cycle** for harvested plants:

- Pick a **max** and **min** chamber humidity.
- When humidity rises to **max**, turn the AC Infinity fan **on** at a set level.
- Run until humidity falls to **min**, then turn the fan **off**.
- Wait for humidity to climb back to **max**, then repeat (hysteresis loop).
- **Separate max intensity for day vs night** (gentler airflow at night), each
  adjustable from a web-UI slider.

The Controller 69 has no local server but is controllable via AC Infinity's cloud
REST API (`http://www.acinfinityserver.com`, plain HTTP, form-encoded), so HTTP
control is feasible.

## Design evolution: why a Hetzner middleware

**Initial idea (on-device direct):** put the AC Infinity cloud client directly in
the ESP32's HAL, alongside the existing Shelly HTTP code. `HAL::setFan()` would log
in, GET the port's settings, and re-POST them with the speed changed.

**Why we pivoted:** AC Infinity's `addDevMode` is a **full-state overwrite, not a
patch**. To change just the fan speed you must echo back the port's *entire* ~100-
field settings object (timers, schedules, VPD, triggers — not just speed). On
firmware that means:

- **Silent corruption risk** — drop a field or coerce a type wrong (int/float,
  `null`/empty, bool as `0/1`) and you don't get "speed unchanged", you get the
  port's schedule/VPD config quietly mangled.
- **API drift** — reverse-engineered + undocumented; an app/firmware update that
  adds a field breaks the round-trip.
- **Embedded cost & slow iteration** — multi-KB ArduinoJson parse + URL-encode
  rebuild on the ESP32-C6, debugged through ~70s build/flash cycles.
- **Token lifecycle + 2 blocking cloud round-trips** per fan change.

**The pivot:** the user already runs a **Hetzner backend with an MQTT broker**
(see `secrets.yaml` mqtt config). Moving the AC Infinity client there lets us run
the reference Python client (`Brysshmurda/ac-infinity-standalone-controller`)
**verbatim**, keep credentials off-device, centralize token refresh / retries /
API-drift fixes, and reduce the ESP32 side to a trivial, stable command. It does
**not** break the HAL→ASG→Controller layering — HAL still owns the outbound call,
it just targets our own clean endpoint instead of the raw AC Infinity API.

**Decisions (from user):** humidity = existing `bme280_humidity`; day/night = reuse
controller `isNightModeHours()`; intensity = native AC Infinity levels **0–10**.

## Architecture

The fan is just another actuator and follows the existing 3-layer flow, exactly
like the Shelly relays:

```
Controller (new DRYING states)
  → ActuatorSafetyGate.executeFanCommand(level)
    → HAL.setFan(level)                         [trivial: publish/POST to Hetzner]
        → Hetzner middleware (Python AC Infinity client)
            → AC Infinity cloud HTTP (full-blob addDevMode handled here)
```

ASG has no blockers today, but `executeFanCommand` is the seam for future
critical-humidity limits. Humidity is read by the controller **through HAL**
(like `readPH`/`readTemperature`), sourced from `bme280_humidity`.

## Hetzner middleware (new — primary work)

A small service on the existing Hetzner host wraps the reference client.

- **Reuse `ac_infinity_client.py` essentially as-is** (login, `devInfoListAll`,
  `getdevModeSettingList`, full-blob `addDevMode`, `atType` 1=OFF/2=ON,
  `onSpead` 0–10). All full-blob fidelity, JSON handling, token caching/refresh,
  and API-drift fixes live here.
- **Credentials** (AC Infinity email/password, devId, port) live on the server,
  not on the ESP32.
- **Ingress from PlantOS — prefer MQTT** (broker already configured with TLS CA
  cert in `secrets.yaml`):
  - Command topic e.g. `plantos/drying/fan/set` with payload = level `0–10`.
  - Service subscribes, translates to the AC Infinity API, and (optionally)
    publishes an ack/state topic `plantos/drying/fan/state`.
  - HTTP fallback: `POST /fan {"level": <0-10>}` on the Hetzner host if MQTT is
    unavailable.
- **Discovery once:** a server-side command/log of `devInfoListAll` to find
  `devId`/`port`; these stay in server config, not device secrets.
- Service should retry/rate-limit and be idempotent on repeated identical levels.

## Implementation by layer (ESP32)

### Layer 3 — HAL (`components/plantos_hal/`)

- **`hal.h`** — new pure-virtual methods on `HAL`, implemented in `ESPHomeHAL`:
  - `virtual void setFan(int level) = 0;` — 0 = off, 1–10 = on at that level.
    Implementation is **trivial**: publish `level` to the MQTT command topic (or
    POST to the Hetzner endpoint). No AC Infinity logic on-device.
  - `virtual float readHumidity() = 0;` + `virtual bool hasHumidityValue() const = 0;`
    (mirror `readTemperature`/`hasTemperature` at `hal.h:401-407`), backed by the
    injected `bme280_humidity` sensor.
- **`hal.cpp`** — `setFan()` = MQTT publish (reuse the existing MQTT client) or a
  one-line HTTP POST via the injected `http_request` (`CONNECTION_CLOSE_HEADERS`,
  `canSendHttpRequest` guards at `hal.cpp:20`). `readHumidity()` returns the sensor
  state (guard NaN).
- **`__init__.py`** — DI per the established pattern: inject `humidity_sensor`
  (`use_id(sensor.Sensor)`); add config for the fan command topic / endpoint URL.

### Layer 2 — ActuatorSafetyGate (`components/actuator_safety_gate/`)

- Add `bool executeFanCommand(int level);` alongside `executeCommand`. Validates,
  debounces repeated identical levels, then calls `hal_->setFan(level)`. Register a
  `"DryingFan"` actuator id so runtime/duration tracking + future humidity blocking
  have a home.

### Layer 1 — Controller (`components/plantos_controller/`)

- **`controller.h`** — add two states to `ControllerState` (enum at `controller.h:47`):
  `DRYING_IDLE` (fan off, waiting humidity ≥ max) and `DRYING_VENTILATE` (fan on,
  waiting humidity ≤ min). Add handlers `handleDryingIdle()` /
  `handleDryingVentilate()` (near `handle*()` at `controller.h:749-770`). Add public
  API `void startDrying();` / `void stopDrying();` and config setters
  `setDryingHumidityMin/Max(float)`, `setDryingDayLevel/NightLevel(int)`. Add a
  `requestFan(int level)` wrapper mirroring `requestPump`/`requestValve`
  (`controller.h:851,860`) calling `safety_gate_->executeFanCommand(level)`.
- **`controller.cpp`**:
  - Add the two `case ControllerState::DRYING_*` entries to the loop dispatch
    switch (`controller.cpp:299-383`) and to `getStateAsString()` (`:3577`).
  - `startDrying()` → `transitionTo(DRYING_IDLE)`; `stopDrying()` → `requestFan(0)`
    + `transitionTo(IDLE)`.
  - `handleDryingIdle()`: read `hal_->readHumidity()`; if `>= humidity_max_` →
    `requestFan(level)` where `level = isNightModeHours() ? night_level_ : day_level_`
    → `transitionTo(DRYING_VENTILATE)`. Poll on a `millis()` interval (~30s) like
    other handlers; enforce a min dwell to avoid command flapping.
  - `handleDryingVentilate()`: if humidity `<= humidity_min_` → `requestFan(0)` →
    `transitionTo(DRYING_IDLE)`. If day/night flips mid-cycle, re-issue
    `requestFan(new level)`.
- **LED behavior** — map the two new states in the LED behavior system
  (`led_behavior*` + `led_behaviors/`), reusing existing behaviors (e.g. ventilate
  = Blue Pulse, idle-drying = Dim Blue Breathing).
- **`FSMINFO.md`** — MUST update (per CLAUDE.md): add DRYING_IDLE / DRYING_VENTILATE
  to the state diagram, actuator-actions table (DryingFan on/off + level), timeout
  table, and the public-API/external-triggers table.

### `plantOS.yaml`

- HAL block: add `humidity_sensor: bme280_humidity` and the fan command topic /
  endpoint config.
- Controller config: `drying_humidity_min`, `drying_humidity_max`,
  `drying_day_level`, `drying_night_level` (initial defaults; sliders override).
- Web UI (mirror sliders at `plantOS.yaml:2731-2841`, buttons at `784-1002`):
  - 4 `number` template sliders → `setDryingHumidityMin/Max(x)`,
    `setDryingDayLevel/NightLevel((int)x)` (humidity 0–100 step 1; level 0–10 step 1,
    `mode: slider`, `restore_value: true`).
  - 2 `button`s → `startDrying()` / `stopDrying()`.

## Files to modify / create

**ESP32 / repo:**
- **Edit** `components/plantos_hal/hal.h`, `hal.cpp`, `__init__.py`
- **Edit** `components/actuator_safety_gate/actuator_safety_gate.h`, `.cpp`
- **Edit** `components/plantos_controller/controller.h`, `controller.cpp`, LED files
- **Edit** `FSMINFO.md` (authoritative — required)
- **Edit** `plantOS.yaml` (HAL config, controller config, sliders/buttons)
- **Edit** `CLAUDE.md` (document new states + DryingFan actuator)

**Hetzner (separate deploy):**
- AC Infinity bridge service (Python, based on `ac_infinity_client.py`) +
  MQTT subscriber / small HTTP endpoint, with AC Infinity creds in server config.

## Key references to reuse

- MQTT client + HTTP-from-HAL machinery: `components/plantos_hal/hal.cpp:20,228,498`
- HAL DI pattern: `components/plantos_hal/__init__.py`
- ASG entry point: `ActuatorSafetyGate::executeCommand` (`actuator_safety_gate.h:214`)
- Controller ASG wrappers: `requestPump`/`requestValve` (`controller.h:851,860`)
- Day/night: `PlantOSController::isNightModeHours()` (`controller.h:175`)
- State dispatch/string: `controller.cpp:299-383`, `getStateAsString()` `:3577`
- Slider/button YAML: `plantOS.yaml:2731-2841`, `784-1002`
- Reference cloud client: `Brysshmurda/ac-infinity-standalone-controller/ac_infinity_client.py`

## Risks / notes

- **Added dependency:** if Hetzner/MQTT is down, fan control is down. Mitigated by
  the fact that the device already depends on internet + AC Infinity's own cloud;
  the only *new* fragility is a service we control. Keep the drying FSM fail-safe
  (on comms loss, hold last state / log, don't crash).
- **Full-blob fidelity** now lives server-side where it can be iterated and tested
  without reflashing — first log raw `getdevModeSettingList` from the real unit and
  match the reference client's field list there.
- Device `setFan()` is non-blocking-ish (MQTT publish) or a single short POST — no
  cloud round-trips on the ESP32 loop.

## Verification

1. **Middleware first:** on Hetzner, confirm the Python bridge can log in and set
   the fan on/off at a given level (verify in the AC Infinity app).
2. Confirm the bridge reacts to the MQTT command topic (publish `6`, fan spins;
   publish `0`, fan stops) and publishes state.
3. `task build` — ESP32 compiles clean (~70s).
4. `task run` — flash + logs. Web UI: set `drying_humidity_max` just below current
   room humidity, press **Start Drying** → controller enters `DRYING_VENTILATE`,
   publishes the day/night level, fan turns on.
5. Force humidity ≤ min → fan off, state → `DRYING_IDLE`.
6. Enter night-mode hours (or set night window to now) → running cycle switches to
   `night_level`.
7. Kill the bridge / MQTT → controller logs comms failure, stays safe, no watchdog
   reset.
