# PlantOS ESP32-C6 GPIO Pinout Reference
This file documents the current pinout of the esp32 and explains the connected devices. For a guide on how to change the pinout in the code, see PINOUT_CHANGE.md.
**Waveshare ESP32-C6-DEV-KIT-N8 Hardware Configuration**

Last Updated: 2025-01-23
Firmware: PlantOS v1.0

---

## Quick Reference Table

### Active GPIO Assignments

| GPIO | Function | Component | Direction | HAL Mapping | Pull-up Required |
|------|----------|-----------|-----------|-------------|------------------|
| **ACTUATORS (Top of PCB)** |||||
| GPIO1 | Water Valve | MagValve | Output | `mag_valve_output_` | No |
| GPIO4 | Acid Pump | PP_1 | Output | `pump_ph_output_` | No |
| GPIO5 | Nutrient Pump A (Grow) | PP_2 | Output | `pump_grow_output_` | No |
| GPIO6 | Nutrient Pump B (Micro) | PP_3 | Output | `pump_micro_output_` | No |
| GPIO7 | Nutrient Pump C (Bloom) | PP_4 | Output | `pump_bloom_output_` | No |
| **SYSTEM** |||||
| GPIO8 | System LED | WS2812 RGB (RMT) | Output | `led_` | No |
| **SENSORS (Bottom of PCB)** |||||
| GPIO0 | TDS Sensor | WTR_TDS (Analog) | `ec_sensor_`| No |
| GPIO3 | DS18B20 Water Temp | WTR_TEMP (1-Wire) | Bidirectional | `temperature_sensor_` | ✅ 4.7kΩ to 3.3V |
| GPIO17 | Water Level HIGH | WTR_HI | Input | `water_level_high_sensor_` | No |
| GPIO18 | EZO pH UART TX | ESP→EZO | Output | `ph_sensor_component_` | No |
| GPIO19 | EZO pH UART RX | ESP←EZO | Input | `ph_sensor_component_` | No |
| GPIO20 | I2C SDA | AIR_TEMP (BME280) | Bidirectional | - | ✅ 4.7kΩ to 3.3V built-in |
| GPIO21 | I2C SCL | AIR_TEMP (BME280) | Output | - | ✅ 4.7kΩ to 3.3V built-in |
| GPIO22 | Water Level EMPTY | WTR_Empty | Input | `water_level_empty_sensor_` | No |
| GPIO23 | Water Level LOW | WTR_LO | Input | `water_level_low_sensor_` | No |

[Waveshare ESP32-C6-DEV-KIT-N8 Official Pinout](https://www.waveshare.com/wiki/ESP32-C6-DEV-KIT-N8#Pinout)

---

## GPIO Layout (PCB Silkscreen Reference)

```
ACTUATOR SIDE (Top of board)
┌─────────────────────────────────┐
│  GPIO1  - Water Valve (MagValve)│
│  GPIO4  - Acid Pump (PP_1)      │
│  GPIO5  - Nutrient Pump A (PP_2)│
│  GPIO6  - Nutrient Pump B (PP_3)│
│  GPIO7  - Nutrient Pump C (PP_4)│
└─────────────────────────────────┘

SENSOR SIDE (Bottom of board)
┌─────────────────────────────────┐
│  GPIO3  - DS18B20 (WTR_TEMP)    │
│  GPIO17 - Water Level HIGH      │
│  GPIO18 - EZO pH TX (ESP→EZO)   │
│  GPIO19 - EZO pH RX (ESP←EZO)   │
│  GPIO20 - I2C SDA (AIR_TEMP)    │
│  GPIO21 - I2C SCL (AIR_TEMP)    │
│  GPIO22 - Water Level EMPTY     │
│  GPIO23 - Water Level LOW       │
└─────────────────────────────────┘

UNCHANGED
┌─────────────────────────────────┐
│  GPIO8  - System LED (WS2812)   │
└─────────────────────────────────┘
```

---

## Detailed Pin Descriptions

### 📡 Communication Interfaces

#### UART Bus (EZO pH Sensor)
- **GPIO18** (TX): ESP32 → EZO RX
- **GPIO19** (RX): ESP32 ← EZO TX
- **Baud Rate**: 9600 (8N1)
- **Component**: `ezo_ph_uart` (Atlas Scientific EZO pH circuit)
- **Notes**:
  - Critical 300ms delay after write before reading
  - Temperature compensation supported
  - Three-point calibration (pH 4.0, 7.0, 10.01)

#### I2C Bus (BME280 Environmental Sensor)
- **GPIO20** (SDA): Serial Data Line
- **GPIO21** (SCL): Serial Clock Line
- **Frequency**: 100kHz (Standard Mode)
- **Pull-ups**: 4.7kΩ resistors to 3.3V **REQUIRED**
- **PCB Label**: AIR_TEMP
- **Components**:
  - BME280 (temperature, humidity, pressure)
  - I2C scanner (diagnostic)
- **Notes**: Can support multiple devices (future expansion)

#### 1-Wire Bus (Temperature Sensor)
- **GPIO3**: DS18B20 Digital Temperature Sensor
- **PCB Label**: WTR_TEMP
- **PCB Label**: WTR_TEMP
- **Pull-up**: 4.7kΩ resistor to 3.3V **REQUIRED**
- **Component**: `dallas_temp` (ESPHome built-in)
- **Accuracy**: ±0.5°C (range: -10°C to +85°C)
- **Purpose**: Temperature compensation for pH sensor

---

### 🎯 Sensor Inputs

#### Binary Sensors (Water Level)

**GPIO17 - Water Level HIGH Sensor (WTR_HI)**
- **Type**: Binary input
- **Hardware**: XKC-Y23-V 3.3V-compatible capacitive level sensor
- **Purpose**: Prevent tank overflow during WATER_FILLING
- **Safety**: Controller aborts fill when HIGH detected (sensor ON = water present)
- **HAL ID**: `water_level_high_sensor_`
- **Debounce**: 500ms filter in ESPHome to prevent water ripple false triggers

**GPIO23 - Water Level LOW Sensor (WTR_LO)**
- **Type**: Binary input
- **Hardware**: XKC-Y23-V 3.3V-compatible capacitive level sensor
- **Purpose**: Auto-feed trigger level
- **Safety**: Controller pre-checks before starting pump, aborts empty when LOW clears
- **HAL ID**: `water_level_low_sensor_`
- **Debounce**: 500ms filter in ESPHome to prevent water ripple false triggers

**GPIO22 - Water Level EMPTY Sensor (WTR_Empty)**
- **Type**: Binary input
- **Hardware**: XKC-Y23-V 3.3V-compatible capacitive level sensor
- **Purpose**: Minimum safe level (prevents pump dry-running)
- **Safety**: Controller aborts operations when water falls below this level
- **HAL ID**: `water_level_empty_sensor_`
- **Debounce**: 500ms filter in ESPHome to prevent water ripple false triggers

**Sensor Logic**:
- HIGH=ON, LOW=ON → Tank FULL (water above HIGH level)
- HIGH=OFF, LOW=ON → Tank NORMAL (water between HIGH and LOW)
- HIGH=OFF, LOW=OFF, EMPTY=ON → Tank LOW (auto-feed trigger zone)
- HIGH=OFF, LOW=OFF, EMPTY=OFF → Tank EMPTY (danger - below safe level)
- HIGH=ON, LOW=OFF → ERROR (invalid state - check wiring)

---

### 💡 User Feedback

**GPIO8 - System Status LED (WS2812)**
- **Type**: Addressable RGB LED (built-in to ESP32-C6-DevKitC-1)
- **Protocol**: RMT peripheral (hardware-driven)
- **RGB Order**: RGB
- **HAL Mapping**: `led_` → `setSystemLED(r, g, b, brightness)`
- **Controller**: Driven by FSM LED behavior system (12 states)
- **⚠️ Strapping Pin**: Warning is expected and safe for this use case
- **Transition**: 0s (instant) - Controller handles all animations

---

### 🔌 Actuator Outputs (Active HIGH Logic)

All actuators use **active HIGH** logic:
- **3.3V (HIGH) = ON/OPEN**
- **0V (LOW) = OFF/CLOSED**

⚠️ **External relay board required** - ESP32 GPIO cannot source enough current for pumps/valves directly.

**NOTE**: AirPump and WastewaterPump are controlled via Shelly Plus 4PM (HTTP).

#### GPIO1 - Water Valve (MagValve)
- **HAL ID**: `WaterValve`
- **Output**: `mag_valve_output`
- **Purpose**: Fresh water inlet for tank filling
- **Max Duration**: 600s (10 minutes)
- **Control**: WATER_FILLING state → SafetyGate → HAL

#### GPIO4 - Acid Pump (PP_1)
- **HAL ID**: `AcidPump`
- **Output**: `pump_ph_output`
- **Purpose**: pH correction (lowers pH)
- **Max Duration**: 10s
- **Control**: PH_INJECTING state → SafetyGate → HAL
- **Safety**: Soft-start/soft-stop ramping (2000ms)

#### GPIO5 - Nutrient Pump A / Grow (PP_2)
- **HAL ID**: `NutrientPumpA`
- **Output**: `pump_grow_output`
- **Purpose**: Vegetative growth nutrients
- **Calendar Mapping**: `nutrient_A_ml_per_liter`
- **Max Duration**: 10s
- **Control**: FEEDING state → SafetyGate → HAL

#### GPIO6 - Nutrient Pump B / Micro (PP_3)
- **HAL ID**: `NutrientPumpB`
- **Output**: `pump_micro_output`
- **Purpose**: Trace minerals and micronutrients
- **Calendar Mapping**: `nutrient_B_ml_per_liter`
- **Max Duration**: 10s
- **Control**: FEEDING state → SafetyGate → HAL

#### GPIO7 - Nutrient Pump C / Bloom (PP_4)
- **HAL ID**: `NutrientPumpC`
- **Output**: `pump_bloom_output`
- **Purpose**: Flowering/bloom nutrients
- **Calendar Mapping**: `nutrient_C_ml_per_liter`
- **Max Duration**: 10s
- **Control**: FEEDING state → SafetyGate → HAL

---

## Reserved & Unavailable Pins

| GPIO | Status | Reason | Notes |
|------|--------|--------|-------|
| GPIO8 | ⚠️ Strapping Pin | Boot mode | Safe for LED output |
| GPIO9 | ⚠️ Strapping Pin | Boot mode selection | **DO NOT USE** |
| GPIO12 | Reserved | USB Serial D- | System debugging |
| GPIO13 | Reserved | USB Serial D+ | System debugging |
| GPIO15 | ⚠️ Strapping Pin | JTAG | Not used |

**Note**: GPIO4 and GPIO5 are NOT strapping pins on the Waveshare ESP32-C6-DEV-KIT-N8 - safe for actuator use.

---

## Pull-up Resistor Requirements

| GPIO | Component | Pull-up |
|------|-----------|---------|
| GPIO3 | DS18B20 (1-Wire) | 4.7kΩ to 3.3V |
| GPIO20 | I2C SDA | 4.7kΩ to 3.3V |
| GPIO21 | I2C SCL | 4.7kΩ to 3.3V |

---

## Configuration File Mapping

### plantOS.yaml Configuration Sections

**I2C Configuration** (~line 223):
```yaml
i2c:
  id: i2c_bus
  sda: GPIO20
  scl: GPIO21
  frequency: 100kHz
```

**UART Configuration** (~line 231):
```yaml
uart:
  id: uart_bus
  tx_pin: GPIO18
  rx_pin: GPIO19
  baud_rate: 9600
```

**1-Wire Configuration** (~line 363):
```yaml
one_wire:
  - platform: gpio
    pin: GPIO3
    id: one_wire_bus
```

**LED Configuration** (~line 282):
```yaml
light:
  - platform: esp32_rmt_led_strip
    pin: GPIO8
    num_leds: 1
    rgb_order: RGB
    id: system_led
    chipset: WS2812
```

**Actuator Outputs** (~line 313):
```yaml
output:
  - platform: ledc
    id: mag_valve_output
    pin: GPIO1

  - platform: ledc
    id: pump_ph_output
    pin: GPIO4

  - platform: ledc
    id: pump_grow_output
    pin: GPIO5

  - platform: ledc
    id: pump_micro_output
    pin: GPIO6

  - platform: ledc
    id: pump_bloom_output
    pin: GPIO7
```

**Water Level Binary Sensors** (~line 469):
```yaml
binary_sensor:
  - platform: gpio
    id: water_level_high_sensor
    pin: GPIO17

  - platform: gpio
    id: water_level_low_sensor
    pin: GPIO23

  - platform: gpio
    id: water_level_empty_sensor
    pin: GPIO22
```

---

## Safety & Operational Notes

### ⚡ Electrical Safety

1. **External Relay Board Required**
   - ESP32 GPIO max: 40mA per pin, 200mA total
   - Use optocoupler-isolated relay board (e.g., 8-channel 5V relay module)
   - Connect ESP32 GND to relay board GND
   - Power relays from separate 12V/24V supply (NOT from ESP32)

2. **Pull-up Resistors**
   - I2C (GPIO20/21): 4.7kΩ to 3.3V **REQUIRED**
   - 1-Wire (GPIO3): 4.7kΩ to 3.3V **REQUIRED**
   - Missing pull-ups will cause bus timeouts and sensor failures

3. **Strapping Pins**
   - GPIO8 (LED): Safe for output, warning is expected
   - GPIO9: **DO NOT USE** - affects boot mode selection

### 🔒 ActuatorSafetyGate Protection

All actuators flow through Layer 2 safety validation:
- **Debouncing**: 1s minimum between commands
- **Duration Limits**: Enforced per actuator (configurable)
- **Soft-Start/Soft-Stop**: 2000ms PWM ramping (configurable)
- **Runtime Tracking**: Monitors active duration
- **Violation Logging**: All rejections logged with reasons

---

## Version History

- **2025-01-23**: Sensor pinout reorganization for hardware layout changes
  - Water Temp (DS18B20): GPIO18 → GPIO3
  - Water Level HIGH: GPIO19 → GPIO17
  - Water Level LOW: GPIO20 → GPIO23
  - Water Level EMPTY: GPIO21 → GPIO22
  - I2C SCL: GPIO22 → GPIO21
  - I2C SDA: GPIO23 → GPIO20
  - EZO UART TX: GPIO17 → GPIO18
  - EZO UART RX: GPIO16 → GPIO19

- **2025-01-22**: Complete GPIO reassignment for new PCB layout
  - Actuators moved to GPIO1, 4, 5, 6, 7 (top of PCB)
  - Sensors moved to GPIO16-23 (bottom of PCB)
  - Light sensor (KY-046) removed from design
  - I2C moved to GPIO22 (SCL) / GPIO23 (SDA)
  - UART moved to GPIO16 (RX) / GPIO17 (TX)
  - DS18B20 moved to GPIO18 (WTR_TEMP)
  - Water level sensors moved to GPIO19, 20, 21

- **2025-12-30**: Water level sensor implementation complete
  - Configured XKC-Y23-V binary sensors
  - Added HAL methods for water level reading
  - Updated controller handlers with sensor-based abort

- **2025-12-28**: DS18B20 1-Wire timing conflict resolved
  - Previous: DS18B20 on GPIO23

- **2025-12-27**: Initial pinout documentation created

---

## References

- **ESP32-C6 Datasheet**: [Espressif ESP32-C6 Technical Reference](https://www.espressif.com/sites/default/files/documentation/esp32-c6_technical_reference_manual_en.pdf)
- **Waveshare ESP32-C6-DEV-KIT-N8**: [Product Wiki](https://www.waveshare.com/wiki/ESP32-C6-DEV-KIT-N8)
- **PlantOS CLAUDE.md**: Complete system architecture and component documentation
- **HAL Implementation**: `components/plantos_hal/hal.h` and `hal.cpp`
- **Main Configuration**: `plantOS.yaml`
