# PlantOS ESP32-C6 GPIO Pinout Reference

**Board:** Waveshare ESP32-C6-DEV-KIT-N8 ([pinout](https://www.waveshare.com/wiki/ESP32-C6-DEV-KIT-N8#Pinout)).
The YAML uses the board id `esp32-c6-devkitc-1`.

Last verified against `plantOS.yaml`: 2026-11-14. When this file and the YAML disagree, the YAML wins.

---

## Pin table

| GPIO | Function | PCB label | Direction | YAML id | HAL member | Note |
|------|----------|-----------|-----------|---------|------------|------|
| **Actuators (top of PCB), LEDC PWM at 1000 Hz, active HIGH** |||||||
| GPIO1 | Water valve | MagValve | Output | `mag_valve_output` | `mag_valve_output_` | |
| GPIO4 | Acid pump | PP_1 | Output | `pump_ph_output` | `pump_ph_output_` | |
| GPIO5 | Nutrient pump A (Grow) | PP_2 | Output | `pump_grow_output` | `pump_grow_output_` | |
| GPIO6 | Nutrient pump B (Micro) | PP_3 | Output | `pump_micro_output` | `pump_micro_output_` | |
| GPIO7 | Nutrient pump C (Bloom) | PP_4 | Output | `pump_bloom_output` | `pump_bloom_output_` | |
| **System** |||||||
| GPIO8 | System LED, WS2812 (RMT) | on board | Output | `system_led` | `led_` | Strapping pin; the boot warning is expected |
| **Sensors (bottom of PCB)** |||||||
| GPIO0 | TDS/EC analog (KS0429) | WTR_TDS | Input (ADC1_CH0) | `tds_adc_raw` → `ec_sensor` | `ec_sensor_` | |
| GPIO3 | DS18B20 water temperature (1-Wire) | WTR_TEMP | Bidirectional | `one_wire_bus` → `water_temperature` | `temperature_sensor_` | 4.7 kΩ pull-up to 3.3 V |
| GPIO17 | Water level HIGH (XKC-Y23-V) | WTR_HI | Input, pull-down | `water_level_high_sensor` | `water_level_high_sensor_` | |
| GPIO23 | Water level LOW (XKC-Y23-V) | WTR_LO | Input, pull-down | `water_level_low_sensor` | `water_level_low_sensor_` | |
| GPIO22 | Water level EMPTY (XKC-Y23-V) | WTR_Empty | Input, pull-down | `water_level_empty_sensor` | `water_level_empty_sensor_` | |
| GPIO18 | EZO pH UART TX (ESP → EZO) | | Output | `uart_bus` | `ph_sensor_component_` | 9600 baud, 8N1 |
| GPIO19 | EZO pH UART RX (ESP ← EZO) | | Input | `uart_bus` | `ph_sensor_component_` | |
| GPIO20 | I2C SDA (BME280 at 0x76) | AIR_TEMP | Bidirectional | `i2c_bus` | - | 4.7 kΩ pull-up to 3.3 V |
| GPIO21 | I2C SCL (BME280 at 0x76) | AIR_TEMP | Output | `i2c_bus` | - | 4.7 kΩ pull-up to 3.3 V, 100 kHz |

Not on a GPIO: **AirPump** (socket 0), **WastewaterPump** (socket 2) and **GrowLight** (socket 3) are
switched by a Shelly Plus 4PM at 192.168.0.130 over HTTP.

### Reserved and unavailable pins

| GPIO | Reason |
|------|--------|
| GPIO9 | Strapping pin (boot mode). **Do not use.** |
| GPIO12, GPIO13 | USB serial D- / D+ |
| GPIO15 | Strapping pin (JTAG) |

GPIO4 and GPIO5 are not strapping pins on this board.

---

## Notes per device

**Water level sensors (GPIO17 / 23 / 22).** XKC-Y23-V capacitive sensors, run at 3.3 V.
Sensor ON = water present. Each input has a 500 ms `delayed_on` / `delayed_off` filter against ripple.

| HIGH | LOW | EMPTY | Meaning |
|------|-----|-------|---------|
| ON | ON | any | FULL |
| OFF | ON | any | NORMAL |
| OFF | OFF | ON | LOW (auto-feed trigger zone) |
| OFF | OFF | OFF | EMPTY (below the safe level) |
| ON | OFF | any | invalid, check wiring |

**EZO pH (GPIO18 / 19).** Atlas Scientific EZO pH circuit in UART mode through an ADUM1201
isolator, component `ezo_ph_uart`. The circuit needs 300 ms after a command before it answers.
Temperature compensation from the DS18B20. Three-point calibration (pH 4.00, 7.00, 10.01).

**DS18B20 (GPIO3).** ESPHome `dallas_temp`. Used for pH and EC temperature compensation.

**TDS/EC (GPIO0).** KS0429 TDS Meter V1.0, output 0–2.3 V. Raw ADC comes from the leon-v ESPHome
fork (the stock ADC does not compile on the C6). Details: `docs/EC_Sensor_Calibration.md`.

**System LED (GPIO8).** WS2812 on the board, RGB order `RGB`, driven by the controller's LED behaviors.

**Actuator outputs (GPIO1, 4, 5, 6, 7).** 3.3 V logic-level MOSFETs switch the pumps and the valve;
the GPIOs only drive the gates. HIGH = on. The pumps and the valve need their own supply with a
common ground.

Maximum on-times enforced by the ActuatorSafetyGate (set in the `on_boot` lambda of `plantOS.yaml`):

| Actuator | Max duration |
|---|---|
| WaterValve | 600 s |
| AcidPump | 30 s |
| NutrientPumpA / B / C | 120 s each |
| WastewaterPump | 1800 s |
| AirPump | unlimited |

The safety gate also rejects commands that match the current state. Its soft-start / soft-stop
PWM ramp exists in the code, but no ramp duration is configured (`setRampDuration()` is never
called), so the outputs switch at once.

---

## Changing a pin

1. `plantOS.yaml`: the pin itself (`i2c`, `uart`, `one_wire`, `light`, `output`, `sensor`,
   `binary_sensor`) and the comments that name it, including the `plantos_hal:` block.
2. `packages/debug.yaml`: the direct GPIO switches and test buttons.
3. Comments and log texts that contain the GPIO number in `components/plantos_hal/` and
   `components/plantos_controller/`.
4. This file and the pin table in `AGENTS.md`.

Find every mention with `grep -rn "GPIO<old>" plantOS.yaml packages components *.md`.
Check the strapping and USB pins above before choosing a new pin, and make sure no GPIO is used twice.
A new sensor or actuator additionally needs a HAL method and its injection in
`components/plantos_hal/__init__.py` (see "Architecture" in `AGENTS.md`).

---

## History

- **2026-03-04**: TDS/EC sensor added on GPIO0.
- **2026-01-23**: Sensor pins reorganized for the PCB layout: DS18B20 GPIO18 → GPIO3, level HIGH
  GPIO19 → GPIO17, level LOW GPIO20 → GPIO23, level EMPTY GPIO21 → GPIO22, I2C SCL GPIO22 → GPIO21,
  I2C SDA GPIO23 → GPIO20, EZO TX GPIO17 → GPIO18, EZO RX GPIO16 → GPIO19.
- **2026-01-22**: Complete reassignment for the new PCB: actuators to GPIO1, 4, 5, 6, 7 (top),
  sensors to GPIO16-23 (bottom). The KY-046 light sensor was removed.
- **2025-12-30**: Water level sensors (XKC-Y23-V) added.
- **2025-12-28**: DS18B20 moved (then to GPIO23) to resolve a 1-Wire timing conflict.
- **2025-12-27**: First version of this file.
