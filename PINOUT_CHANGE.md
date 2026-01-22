# PlantOS GPIO Pinout Change Guide

This guide documents all locations in the codebase that must be updated when changing GPIO pin assignments.

---

## Quick Checklist

When changing a GPIO pin, update these files in order:

1. `plantOS.yaml` - Main configuration (multiple sections)
2. `components/plantos_hal/__init__.py` - Python schema (if adding/removing sensors)
3. `components/plantos_hal/hal.h` - C++ header comments
4. `components/plantos_hal/hal.cpp` - C++ implementation comments/logs
5. `PINOUT.md` - Documentation

---

## File-by-File Reference

### 1. plantOS.yaml

This is the primary configuration file. Most GPIO changes happen here.

#### I2C Bus Configuration
**Location**: ~line 221-228
```yaml
i2c:
  id: i2c_bus
  sda: GPIO23  # ← Change SDA pin here
  scl: GPIO22  # ← Change SCL pin here
```

#### UART Configuration (EZO pH Sensor)
**Location**: ~line 230-238
```yaml
uart:
  id: uart_bus
  tx_pin: GPIO17  # ← Change TX pin here
  rx_pin: GPIO16  # ← Change RX pin here
```

#### 1-Wire Configuration (DS18B20 Temperature)
**Location**: ~line 363-366
```yaml
one_wire:
  - platform: gpio
    pin: GPIO18  # ← Change 1-Wire pin here
```

#### Actuator PWM Outputs
**Location**: ~line 313-342
```yaml
output:
  - platform: ledc
    id: mag_valve_output
    pin: GPIO1   # ← Water Valve pin

  - platform: ledc
    id: pump_ph_output
    pin: GPIO4   # ← Acid Pump pin

  - platform: ledc
    id: pump_grow_output
    pin: GPIO5   # ← Nutrient Pump A pin

  - platform: ledc
    id: pump_micro_output
    pin: GPIO6   # ← Nutrient Pump B pin

  - platform: ledc
    id: pump_bloom_output
    pin: GPIO7   # ← Nutrient Pump C pin
```

#### Binary Sensors (Water Level)
**Location**: ~line 469-534
```yaml
binary_sensor:
  - platform: gpio
    id: water_level_high_sensor
    pin:
      number: GPIO19  # ← Water HIGH pin

  - platform: gpio
    id: water_level_low_sensor
    pin:
      number: GPIO20  # ← Water LOW pin

  - platform: gpio
    id: water_level_empty_sensor
    pin:
      number: GPIO21  # ← Water EMPTY pin
```

#### HAL Dependency Injection Comments
**Location**: ~line 1322-1340
Update the GPIO comments in the `plantos_hal:` section:
```yaml
plantos_hal:
  temperature_sensor: water_temperature  # DS18B20 on GPIO18
  water_level_high_sensor: water_level_high_sensor  # GPIO19
  water_level_low_sensor: water_level_low_sensor    # GPIO20
  water_level_empty_sensor: water_level_empty_sensor # GPIO21
  mag_valve_output: mag_valve_output   # GPIO1
  pump_ph_output: pump_ph_output       # GPIO4
  pump_grow_output: pump_grow_output   # GPIO5
  pump_micro_output: pump_micro_output # GPIO6
  pump_bloom_output: pump_bloom_output # GPIO7
```

#### Direct GPIO Control Switches (Section 14)
**Location**: ~line 2393-2456
Update switch IDs and names:
```yaml
  - platform: template
    id: direct_gpio1_switch           # ← Update ID
    name: "14_01_Set GPIO1 - ..."     # ← Update name
    turn_on_action:
      - logger.log: "DIRECT GPIO1..." # ← Update log message
```

Repeat for all 5 actuator switches.

#### PWM Control Sliders (Section 15)
**Location**: ~line 2522-2621
Update slider IDs and names:
```yaml
  - platform: template
    id: pwm_gpio1_slider              # ← Update ID
    name: "15_01_GPIO1 - ..."         # ← Update name
    set_action:
      - logger.log:
          format: "PWM GPIO1..."      # ← Update log message
```

Repeat for all 5 actuator sliders.

#### Pump Test Buttons (Section 12)
**Location**: ~line 1006-1116
Update slider references in lambda code:
```yaml
  - lambda: |-
      float pwm_percent = id(pwm_gpio4_slider).state;  # ← Update slider ID
```

#### GPIO Status Logger
**Location**: ~line 2670-2714
Update all `log_gpio()` calls:
```cpp
log_gpio(1, "Water Valve (MagValve)");
log_gpio(4, "Acid Pump (PP_1)");
// ... etc
```

#### Sensor/Component Comments
Search for GPIO references in comments throughout the file:
- DS18B20 sensor section (~line 370-387)
- BME280 sensor section (~line 389-428)
- Binary sensor section headers (~line 453-468)

---

### 2. components/plantos_hal/__init__.py

Only modify if adding/removing sensor types (not for simple pin changes).

#### Configuration Keys
**Location**: ~line 15-20
```python
CONF_LIGHT_SENSOR = 'light_sensor'  # Remove if removing sensor
```

#### Config Schema
**Location**: ~line 35-50
```python
cv.Optional(CONF_LIGHT_SENSOR): cv.use_id(sensor.Sensor),  # Remove line
```

#### Dependency Injection
**Location**: ~line 70-90
```python
if CONF_LIGHT_SENSOR in config:
    light_sensor = await cg.get_variable(config[CONF_LIGHT_SENSOR])
    cg.add(var.set_light_sensor(light_sensor))  # Remove block
```

---

### 3. components/plantos_hal/hal.h

Update comments referencing GPIO pins.

#### Virtual Method Comments
**Location**: ~line 50-80
```cpp
/**
 * Read water level HIGH sensor (XKC-Y23-V on GPIO19 - WTR_HI)
 */
virtual bool readWaterLevelHigh() = 0;
```

#### Setter Declarations (if removing sensors)
**Location**: ~line 150-170
```cpp
void set_light_sensor(esphome::sensor::Sensor* light_sensor);  // Remove
```

#### Member Variables (if removing sensors)
**Location**: ~line 200-220
```cpp
esphome::sensor::Sensor* light_sensor_{nullptr};  // Remove
```

---

### 4. components/plantos_hal/hal.cpp

Update log messages and comments referencing GPIO pins.

#### Setter Implementations
**Location**: ~line 50-100
```cpp
void ESPHomeHAL::set_mag_valve_output(esphome::output::FloatOutput* output) {
    mag_valve_output_ = output;
    ESP_LOGI(TAG, "Magnetic valve output configured (GPIO1 - MagValve)");  // ← Update
}
```

Update all setter log messages for:
- `set_mag_valve_output` - GPIO1
- `set_pump_ph_output` - GPIO4
- `set_pump_grow_output` - GPIO5
- `set_pump_micro_output` - GPIO6
- `set_pump_bloom_output` - GPIO7
- `set_water_level_high_sensor` - GPIO19
- `set_water_level_low_sensor` - GPIO20
- `set_water_level_empty_sensor` - GPIO21

#### setPump/setValve Comments
**Location**: ~line 200-300
```cpp
if (pumpId == "AcidPump") {
    // pH pump (acid dosing) on GPIO4 (PP_1)  // ← Update comment
```

#### setup() Warnings (if removing sensors)
**Location**: ~line 400-450
```cpp
if (!light_sensor_) {
    ESP_LOGW(TAG, "Light sensor not configured...");  // Remove block
}
```

#### Stub Methods (if removing sensors)
**Location**: varies
```cpp
float ESPHomeHAL::readLightIntensity() {
    // Light sensor removed - return stub
    return 0.0f;
}
```

---

### 5. PINOUT.md

Update the documentation to match the new pinout.

#### Quick Reference Table
Update the GPIO assignments table at the top.

#### GPIO Layout Diagram
Update the ASCII art diagram showing pin positions.

#### Detailed Pin Descriptions
Update each pin's description section.

#### Configuration File Mapping
Update the YAML snippets showing pin configurations.

#### Pull-up Resistor Requirements
Update if changing I2C or 1-Wire pins.

#### Version History
Add a new entry documenting the change.

---

## Pin Change Workflow

### For Simple GPIO Reassignment (same component, different pin):

1. Edit `plantOS.yaml`:
   - Change the pin number in the component definition
   - Update all comments referencing the old pin
   - Update direct GPIO switches (section 14)
   - Update PWM sliders (section 15)
   - Update GPIO status logger

2. Edit `components/plantos_hal/hal.cpp`:
   - Update log messages in setter functions

3. Edit `PINOUT.md`:
   - Update all documentation

4. Build and test:
   ```bash
   task build
   ```

### For Adding a New Sensor/Actuator:

1. Edit `plantOS.yaml`:
   - Add new component definition with GPIO
   - Add to HAL dependency injection section

2. Edit `components/plantos_hal/__init__.py`:
   - Add configuration key
   - Add to config schema
   - Add dependency injection code

3. Edit `components/plantos_hal/hal.h`:
   - Add virtual method declaration
   - Add setter declaration
   - Add member variable

4. Edit `components/plantos_hal/hal.cpp`:
   - Implement setter with log message
   - Implement virtual method

5. Edit `PINOUT.md`:
   - Document new pin

### For Removing a Sensor/Actuator:

1. Edit `plantOS.yaml`:
   - Remove or comment out component definition
   - Remove from HAL dependency injection

2. Edit `components/plantos_hal/__init__.py`:
   - Remove configuration key
   - Remove from config schema
   - Remove dependency injection code

3. Edit `components/plantos_hal/hal.h`:
   - Keep virtual method (for interface compatibility) or remove
   - Remove setter declaration
   - Remove member variable

4. Edit `components/plantos_hal/hal.cpp`:
   - Update method to return stub value
   - Remove setter implementation
   - Remove setup() warning

5. Edit `PINOUT.md`:
   - Remove from documentation

---

## Common Pitfalls

1. **Forgetting slider references**: Button test lambdas reference PWM slider IDs (e.g., `pwm_gpio4_slider`). Search for all slider references.

2. **Missing log messages**: Log messages in `hal.cpp` setters and `plantOS.yaml` lambdas contain GPIO numbers.

3. **GPIO status logger**: The 30-second interval logger lists all pins explicitly.

4. **Comment drift**: Comments throughout the codebase reference pin numbers. Use grep to find all:
   ```bash
   grep -rn "GPIO18" --include="*.yaml" --include="*.cpp" --include="*.h" --include="*.py"
   ```

5. **Reserved pins**: Check ESP32-C6 strapping pins before assigning:
   - GPIO8, GPIO9, GPIO15 are strapping pins
   - GPIO12, GPIO13 are USB Serial

---

## Verification

After making changes, verify:

1. **Build succeeds**:
   ```bash
   task build
   ```

2. **No duplicate pins**: Check that no GPIO is used twice.

3. **Documentation matches code**: Compare `PINOUT.md` with `plantOS.yaml`.

4. **Search for old pin numbers**:
   ```bash
   grep -rn "GPIO<old_number>" .
   ```

---

*Last Updated: 2025-01-22*
