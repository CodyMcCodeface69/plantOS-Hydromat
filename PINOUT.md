# PlantOS ESP32-C6 GPIO Pinout Reference

**ESP32-C6-DevKitC-1 Hardware Configuration**

Last Updated: 2025-12-27
Firmware: PlantOS v0.9 (MVP)

---

## Quick Reference Table

### Active GPIO Assignments

| GPIO | Function | Component | Direction | Baud/Freq | HAL Mapping | Pull-up Required |
|------|----------|-----------|-----------|-----------|-------------|------------------|
| GPIO0 | Light Sensor | ADC (KY-046) | Input (Analog) | - | `light_sensor_` | No |
| GPIO4 | EZO pH TX | UART | Output | 9600 | `ph_sensor_component_` | No |
| GPIO5 | EZO pH RX | UART | Input | 9600 | `ph_sensor_component_` | No |
| GPIO6 | I2C SDA | I2C Bus | Bidirectional | 100kHz | - | ✅ 4.7kΩ to 3.3V |
| GPIO7 | I2C SCL | I2C Bus | Output | 100kHz | - | ✅ 4.7kΩ to 3.3V |
| GPIO8 | System LED | WS2812 RGB (RMT) | Output | - | `led_` | No |
| GPIO10 | Water Temp | DS18B20 (1-Wire) | Bidirectional | - | `temperature_sensor_` | ✅ 4.7kΩ to 3.3V |
| GPIO11 | Air Pump | GPIO Output | Output | - | `pump_air_switch_` | No |
| GPIO18 | Water Valve | GPIO Output | Output | - | `mag_valve_switch_` | No |
| GPIO19 | Acid Pump | GPIO Output | Output | - | `pump_ph_switch_` | No |
| GPIO20 | Nutrient Pump A | GPIO Output | Output | - | `pump_grow_switch_` | No |
| GPIO21 | Nutrient Pump B | GPIO Output | Output | - | `pump_micro_switch_` | No |
| GPIO22 | Nutrient Pump C | GPIO Output | Output | - | `pump_bloom_switch_` | No |
| GPIO23 | Wastewater Pump | GPIO Output | Output | - | `pump_wastewater_switch_` | No |

---

## Detailed Pin Descriptions

### 📡 Communication Interfaces

#### UART Bus (EZO pH Sensor)
- **GPIO4** (TX): ESP32 → EZO RX
- **GPIO5** (RX): ESP32 ← EZO TX
- **Baud Rate**: 9600 (8N1)
- **Component**: `ezo_ph_uart` (Atlas Scientific EZO pH circuit)
- **Notes**:
  - Critical 300ms delay after write before reading
  - Temperature compensation supported
  - Three-point calibration (pH 4.0, 7.0, 10.01)

#### I2C Bus
- **GPIO6** (SDA): Serial Data Line
- **GPIO7** (SCL): Serial Clock Line
- **Frequency**: 100kHz (Standard Mode)
- **Pull-ups**: 4.7kΩ resistors to 3.3V **REQUIRED**
- **Components**:
  - I2C scanner (diagnostic)
  - I2C mutex demo (future expansion)
- **Notes**: Can support multiple devices (future TDS/EC sensors, etc.)

#### 1-Wire Bus (Temperature Sensor)
- **GPIO10**: DS18B20 Digital Temperature Sensor
- **Pull-up**: 4.7kΩ resistor to 3.3V **REQUIRED**
- **Component**: `dallas_temp` (ESPHome built-in)
- **Accuracy**: ±0.5°C (range: -10°C to +85°C)
- **Purpose**: Temperature compensation for pH sensor

---

### 🎯 Sensor Inputs

#### Analog Sensors

**GPIO0 - Light Intensity Sensor (KY-046)**
- **Type**: ADC1_CH0 analog input
- **Range**: 0-3.3V
- **Update Interval**: 5 seconds
- **Output**: 0-100% (normalized)
- **⚠️ Boot Pin**: Keep floating or HIGH during boot
- **Notes**: Using lboue's ESP32-C6 ADC fork (official ESPHome has bugs)

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

#### GPIO11 - Air Pump
- **HAL ID**: `AirPump`
- **Switch**: `pump_air_switch`
- **Purpose**: Aeration and mixing after pH dosing
- **Max Duration**: Configurable (safety gate enforced)
- **Control**: ActuatorSafetyGate → HAL → GPIO output

#### GPIO18 - Water Valve (Magnetic/Solenoid)
- **HAL ID**: `WaterValve`
- **Switch**: `mag_valve_switch`
- **Purpose**: Fresh water inlet for tank filling
- **Max Duration**: 300s default (5 minutes)
- **Control**: WATER_FILLING state → SafetyGate → HAL

#### GPIO19 - Acid Pump (pH Down)
- **HAL ID**: `AcidPump`
- **Switch**: `pump_ph_switch`
- **Purpose**: pH correction (lowers pH)
- **Max Duration**: 30s default
- **Control**: PH_INJECTING state → SafetyGate → HAL
- **Safety**: Soft-start/soft-stop ramping (2000ms)

#### GPIO20 - Nutrient Pump A (Grow Phase)
- **HAL ID**: `NutrientPumpA`
- **Switch**: `pump_grow_switch`
- **Purpose**: Vegetative growth nutrients
- **Max Duration**: 60s default
- **Control**: FEEDING state → SafetyGate → HAL

#### GPIO21 - Nutrient Pump B (Micronutrients)
- **HAL ID**: `NutrientPumpB`
- **Switch**: `pump_micro_switch`
- **Purpose**: Trace minerals and micronutrients
- **Max Duration**: 60s default
- **Control**: FEEDING state → SafetyGate → HAL

#### GPIO22 - Nutrient Pump C (Bloom Phase)
- **HAL ID**: `NutrientPumpC`
- **Switch**: `pump_bloom_switch`
- **Purpose**: Flowering/bloom nutrients
- **Max Duration**: 60s default
- **Control**: FEEDING state → SafetyGate → HAL

#### GPIO23 - Wastewater Pump
- **HAL ID**: `WastewaterPump`
- **Switch**: `pump_wastewater_switch`
- **Purpose**: Tank drainage/water change
- **Max Duration**: 180s default (3 minutes)
- **Control**: WATER_EMPTYING state → SafetyGate → HAL

---

## Reserved & Unavailable Pins

| GPIO | Status | Reason | Notes |
|------|--------|--------|-------|
| GPIO1 | Reserved | USB Serial/JTAG (D-) | System debugging |
| GPIO2 | Reserved | USB Serial/JTAG (D+) | System debugging |
| GPIO3 | Reserved | USB Serial/JTAG | System debugging |
| GPIO9 | **Strapping Pin** | Boot mode selection | Avoid if possible |
| GPIO12-17 | Available | Future expansion | Use for water level sensors |
| GPIO24+ | N/A | ESP32-C6 has 30 GPIOs max | Check datasheet for availability |

---

## HAL Architecture Cross-Reference

### Layer 3: Hardware Abstraction Layer (HAL)

**File**: `components/plantos_hal/hal.h`

#### Sensor Component References
```cpp
esphome::sensor::Sensor* ph_sensor_{nullptr};                    // EZO pH UART (GPIO4/5)
esphome::sensor::Sensor* light_sensor_{nullptr};                 // KY-046 ADC (GPIO0)
esphome::sensor::Sensor* temperature_sensor_{nullptr};           // DS18B20 (GPIO10)
esphome::ezo_ph_uart::EZOPHUARTComponent* ph_sensor_component_;  // EZO pH Component
```

#### Actuator Switch References
```cpp
esphome::switch_::Switch* mag_valve_switch_{nullptr};       // GPIO18 - WaterValve
esphome::switch_::Switch* pump_ph_switch_{nullptr};         // GPIO19 - AcidPump
esphome::switch_::Switch* pump_grow_switch_{nullptr};       // GPIO20 - NutrientPumpA
esphome::switch_::Switch* pump_micro_switch_{nullptr};      // GPIO21 - NutrientPumpB
esphome::switch_::Switch* pump_bloom_switch_{nullptr};      // GPIO22 - NutrientPumpC
esphome::switch_::Switch* pump_wastewater_switch_{nullptr}; // GPIO23 - WastewaterPump
esphome::switch_::Switch* pump_air_switch_{nullptr};        // GPIO11 - AirPump
```

#### Actuator State Tracking
```cpp
std::map<std::string, bool> pump_states_;   // Tracks ON/OFF state by HAL ID
std::map<std::string, bool> valve_states_;  // Tracks OPEN/CLOSED state by HAL ID
```

---

## Configuration File Mapping

### plantOS.yaml Configuration Sections

**UART Configuration** (lines 220-228):
```yaml
uart:
  id: uart_bus
  tx_pin: GPIO4
  rx_pin: GPIO5
  baud_rate: 9600
```

**I2C Configuration** (lines 199-204):
```yaml
i2c:
  id: i2c_bus
  sda: GPIO6
  scl: GPIO7
  frequency: 100kHz
```

**1-Wire Configuration** (lines 361-364):
```yaml
one_wire:
  - platform: gpio
    pin: GPIO10
    id: one_wire_bus
```

**LED Configuration** (lines 295-310):
```yaml
light:
  - platform: esp32_rmt_led_strip
    pin: GPIO8
    num_leds: 1
    rgb_order: RGB
    id: system_led
    chipset: WS2812
    default_transition_length: 0s  # Instant - FSM controls animations
```

**Actuator Outputs** (lines 325-352):
```yaml
output:
  - platform: gpio
    id: mag_valve_output
    pin: GPIO18

  - platform: gpio
    id: pump_ph_output
    pin: GPIO19

  # ... (GPIO20-23, GPIO11 for remaining actuators)
```

**HAL Dependency Injection** (lines 794-812):
```yaml
plantos_hal:
  id: hal
  system_led: system_led
  ph_sensor: ph_sensor_real
  ph_sensor_component: ezo_ph_uart_component
  light_sensor: light_intensity_raw
  temperature_sensor: water_temperature

  # Actuator switches
  mag_valve_switch: mag_valve_switch        # GPIO18
  pump_ph_switch: pump_ph_switch            # GPIO19
  pump_grow_switch: pump_grow_switch        # GPIO20
  pump_micro_switch: pump_micro_switch      # GPIO21
  pump_bloom_switch: pump_bloom_switch      # GPIO22
  pump_wastewater_switch: pump_wastewater_switch  # GPIO23
  pump_air_switch: pump_air_switch          # GPIO11
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
   - I2C (GPIO6/7): 4.7kΩ to 3.3V **REQUIRED**
   - 1-Wire (GPIO10): 4.7kΩ to 3.3V **REQUIRED**
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

**Max Duration Defaults** (plantOS.yaml lines 12-20):
```yaml
on_boot:
  then:
    - lambda: |-
        id(actuator_safety)->setMaxDuration("WaterValve", 10);
        id(actuator_safety)->setMaxDuration("AcidPump", 10);
        id(actuator_safety)->setMaxDuration("NutrientPumpA", 10);
        id(actuator_safety)->setMaxDuration("NutrientPumpB", 10);
        id(actuator_safety)->setMaxDuration("NutrientPumpC", 10);
```

---

## ⚠️ Known Issues & Future Work

### 🐛 Critical Issue: Water Level Sensor Conflict

**Problem**: CLAUDE.md documentation states:
> Configure binary sensors on GPIO18 (high) and GPIO19 (low)

**Reality**:
- GPIO18 = Water Valve (mag_valve_output)
- GPIO19 = Acid Pump (pump_ph_output)

**Status**: ❌ Water level sensors NOT configured

**Solution**: Use available pins for water level sensors:
- **GPIO12** - Water Level HIGH sensor (XKC-Y23-V)
- **GPIO13** - Water Level LOW sensor (XKC-Y23-V)

**Hardware Requirements**:
- 2x XKC-Y23-V capacitive water level sensors (5V)
- Voltage dividers (5V → 3.3V) or level shifters
- Integration into WATER_FILLING/WATER_EMPTYING FSM handlers

**Reference**: See CLAUDE.md "Critical Blockers" section (blocker #2)

---

## GPIO Status Logger (DEBUG Mode)

**File**: `plantOS.yaml` lines 1366-1410

A periodic logger runs every 30 seconds in DEBUG mode to monitor GPIO states:

```yaml
interval:
  - interval: 30s
    then:
      - lambda: |-
          # Logs all 14 active GPIO pins with current levels
```

**Example Output**:
```
[D][gpio_status:000] === GPIO Status Report ===
[D][gpio_status:000] GPIO[0]  Light Sensor (ADC)    | Level: 0
[D][gpio_status:000] GPIO[4]  EZO pH UART TX        | Level: 1
[D][gpio_status:000] GPIO[5]  EZO pH UART RX        | Level: 1
[D][gpio_status:000] GPIO[6]  I2C SDA               | Level: 1
[D][gpio_status:000] GPIO[7]  I2C SCL               | Level: 1
[D][gpio_status:000] GPIO[8]  System LED (WS2812)   | Level: 0
[D][gpio_status:000] GPIO[10] Water Temp (1-Wire)   | Level: 1
[D][gpio_status:000] GPIO[11] Air Pump              | Level: 0
[D][gpio_status:000] GPIO[18] Water Valve (Mag)     | Level: 0
[D][gpio_status:000] GPIO[19] Acid Pump (pH)        | Level: 0
[D][gpio_status:000] GPIO[20] Nutrient Pump (Grow)  | Level: 0
[D][gpio_status:000] GPIO[21] Nutrient Pump (Micro) | Level: 0
[D][gpio_status:000] GPIO[22] Nutrient Pump (Bloom) | Level: 0
[D][gpio_status:000] GPIO[23] Wastewater Pump       | Level: 0
[D][gpio_status:000] =========================
```

**Note**: Only logs pin levels (0=LOW, 1=HIGH), not input/output enable status.

---

## Version History

- **2025-12-27**: Initial pinout documentation created
  - Cross-referenced with HAL implementation
  - Identified water level sensor GPIO conflict
  - Added GPIO status logger
  - Documented all 14 active GPIO assignments

---

## References

- **ESP32-C6 Datasheet**: [Espressif ESP32-C6 Technical Reference](https://www.espressif.com/sites/default/files/documentation/esp32-c6_technical_reference_manual_en.pdf)
- **ESP32-C6-DevKitC-1 Schematic**: Built-in LED on GPIO8
- **PlantOS CLAUDE.md**: Complete system architecture and component documentation
- **HAL Implementation**: `components/plantos_hal/hal.h` and `hal.cpp`
- **Main Configuration**: `plantOS.yaml`
