# Plan: WebUI PWM Slider as Source of Truth for HAL Pump Intensity

## Context

Sliders 15_01–15_05 in the WebUI currently set the **duty cycle directly on the LEDC output** (`output.set_level`) — they bypass HAL and ActuatorSafetyGate entirely. The HAL reads pump intensity from `pump_configs_`, which is populated at boot from YAML substitution values (`pump_ph_pwm_intensity: 100%` etc.) via `setPumpConfig()` and then remains static.

**Goal:** Slider value = source of truth for PWM intensity. Whenever ActuatorSafetyGate → HAL activates a pump, the current slider value should be used — including after a reboot.

Bonus: raise PWM frequency from 1000 Hz (audible!) to 25000 Hz to fix the squealing noise from MOSFETs.

---

## Critical Files

| File | Relevant Lines | Content |
|---|---|---|
| `plantOS.yaml` | 313, 319, 325, 331, 337 | LEDC `frequency: 1000 Hz` |
| `plantOS.yaml` | 14–28 | `on_boot` lambda |
| `plantOS.yaml` | 2650–2749 | Slider definitions 15_01–15_05 |
| `components/plantos_hal/hal.cpp` | 264–268 | `setPump(id, state)` reads `pump_configs_` |
| `components/plantos_hal/hal.cpp` | 371–375 | `setPumpConfig()` — writes `pump_configs_` |
| `components/plantos_hal/hal.cpp` | 362–369 | `getPumpConfig()` — reads `pump_configs_` |

**No C++ changes required** — `getPumpConfig()` + `setPumpConfig()` already exist.

---

## Implementation (plantOS.yaml only)

### Step 1 — Raise PWM frequency to 25 kHz (lines 313, 319, 325, 331, 337)

```yaml
# Before:
frequency: 1000 Hz
# After (all 5 outputs, GPIO1 + GPIO4–GPIO7):
frequency: 25000 Hz
```

> GPIO1 (magnetic valve, ON/OFF only) is unaffected by frequency in practice — change for consistency.

### Step 2 — Sliders 15_02–15_05: redirect `set_action` through HAL

For each of the four peristaltic pump sliders (15_02 = AcidPump, 15_03 = NutrientPumpA, 15_04 = NutrientPumpB, 15_05 = NutrientPumpC):

```yaml
# Before:
set_action:
  - output.set_level:
      id: pump_ph_output
      level: !lambda 'return x / 100.0;'
  - logger.log: ...

# After (example AcidPump / 15_02):
set_action:
  - lambda: |-
      auto cfg = id(hal)->getPumpConfig("AcidPump");
      id(hal)->setPumpConfig("AcidPump", cfg.flow_rate_ml_s, x / 100.0f);
      ESP_LOGI("pwm_slider", "AcidPump PWM intensity → %.0f%%", x);
```

> 15_01 (magnetic valve / WaterValve) stays unchanged — it is ON/OFF only, PWM intensity is irrelevant.

### Step 3 — Set slider initial values to calibrated defaults

```yaml
# 15_02 AcidPump:
initial_value: 100   # was: 0  (matches pump_ph_pwm_intensity: 100%)
restore_value: true  # was: false

# 15_03 NutrientPumpA (Grow):
initial_value: 60    # was: 0  (matches pump_grow_pwm_intensity: 60%)
restore_value: true

# 15_04 NutrientPumpB (Micro):
initial_value: 100   # was: 0
restore_value: true

# 15_05 NutrientPumpC (Bloom):
initial_value: 100   # was: 0
restore_value: true
```

### Step 4 — `on_boot` lambda: sync HAL with slider values at startup

Append to the existing `on_boot` lambda (around line 15):

```cpp
// Sync pump PWM intensities from WebUI sliders (slider = source of truth after reboot)
id(hal)->setPumpConfig("AcidPump",
    id(hal)->getPumpConfig("AcidPump").flow_rate_ml_s,
    id(pwm_gpio4_slider).state / 100.0f);
id(hal)->setPumpConfig("NutrientPumpA",
    id(hal)->getPumpConfig("NutrientPumpA").flow_rate_ml_s,
    id(pwm_gpio5_slider).state / 100.0f);
id(hal)->setPumpConfig("NutrientPumpB",
    id(hal)->getPumpConfig("NutrientPumpB").flow_rate_ml_s,
    id(pwm_gpio6_slider).state / 100.0f);
id(hal)->setPumpConfig("NutrientPumpC",
    id(hal)->getPumpConfig("NutrientPumpC").flow_rate_ml_s,
    id(pwm_gpio7_slider).state / 100.0f);
```

> Works because `on_boot priority: -100` runs after all `setup()` calls — by this point `restore_value: true` values are already loaded from flash.

---

## Data Flow After Implementation

```
User moves WebUI slider 15_02 to 70%
    ↓ set_action lambda
    HAL::setPumpConfig("AcidPump", flow_rate, 0.70)
    → pump_configs_["AcidPump"].pwm_intensity = 0.70

Later: ActuatorSafetyGate activates AcidPump
    ↓
    HAL::setPump("AcidPump", true)   ← no intensity param
    → reads pump_configs_["AcidPump"].pwm_intensity = 0.70
    → LEDC set_level(0.70) @ 25 kHz
```

---

## Verification

1. Flash firmware, check serial logs: `on_boot` should print each pump's slider value
2. Move a slider → serial log shows `"AcidPump PWM intensity → 70%"`
3. Press test button 12_02 → pump runs; audible speed change at different slider positions
4. Reboot → slider values restored from flash; `on_boot` log shows correct restored values
5. No more audible squealing from MOSFETs (25 kHz above hearing range)
