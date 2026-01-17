"""
PlantOS Hardware Abstraction Layer (HAL) Component

ESPHome Python integration for the HAL component.
Provides platform-agnostic hardware interface for the unified Controller.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, sensor, binary_sensor, output, switch, http_request, time
from esphome.const import (
    CONF_ID,
)

# Import FloatOutput for PWM pump control
FloatOutput = output.FloatOutput

# Define namespace and classes
plantos_hal_ns = cg.esphome_ns.namespace('plantos_hal')
HAL = plantos_hal_ns.class_('HAL')
ESPHomeHAL = plantos_hal_ns.class_('ESPHomeHAL', HAL, cg.Component)

# Configuration keys
CONF_SYSTEM_LED = 'system_led'
CONF_PH_SENSOR = 'ph_sensor'
CONF_PH_SENSOR_COMPONENT = 'ph_sensor_component'
CONF_LIGHT_SENSOR = 'light_sensor'
CONF_TEMPERATURE_SENSOR = 'temperature_sensor'
CONF_WATER_LEVEL_HIGH_SENSOR = 'water_level_high_sensor'
CONF_WATER_LEVEL_LOW_SENSOR = 'water_level_low_sensor'
CONF_WATER_LEVEL_EMPTY_SENSOR = 'water_level_empty_sensor'
CONF_TIME_SOURCE = 'time_source'
CONF_PH_READING_INTERVAL = 'ph_reading_interval'
CONF_PH_MIN = 'ph_min'
CONF_PH_MAX = 'ph_max'

# Actuator GPIO output keys (Phase 2 - 6 actuators)
# NOTE: Using outputs instead of switches to avoid circular dependency
CONF_MAG_VALVE_OUTPUT = 'mag_valve_output'
CONF_PUMP_PH_OUTPUT = 'pump_ph_output'
CONF_PUMP_GROW_OUTPUT = 'pump_grow_output'
CONF_PUMP_MICRO_OUTPUT = 'pump_micro_output'
CONF_PUMP_BLOOM_OUTPUT = 'pump_bloom_output'
CONF_PUMP_WASTEWATER_OUTPUT = 'pump_wastewater_output'

# Shelly HTTP switch keys (MVP: AirPump and WastewaterPump)
CONF_AIR_PUMP_SWITCH = 'air_pump_switch'
CONF_WASTEWATER_PUMP_SWITCH = 'wastewater_pump_switch'
CONF_HTTP_REQUEST = 'http_request_id'

# Tank volume and valve configuration
CONF_TANK_VOLUME_DELTA_LITERS = 'tank_volume_delta_liters'  # Volume from LOW to HIGH (for normal daily feeding)
CONF_TOTAL_TANK_VOLUME_LITERS = 'total_tank_volume_liters'  # Volume from EMPTY to HIGH (for reservoir change)
CONF_MAG_VALVE_FLOW_RATE = 'mag_valve_flow_rate_ml_s'

# Pump configuration keys (flow rate and PWM intensity for calibration)
CONF_PUMP_PH_FLOW_RATE = 'pump_ph_flow_rate_ml_s'
CONF_PUMP_PH_PWM = 'pump_ph_pwm_intensity'
CONF_PUMP_GROW_FLOW_RATE = 'pump_grow_flow_rate_ml_s'
CONF_PUMP_GROW_PWM = 'pump_grow_pwm_intensity'
CONF_PUMP_MICRO_FLOW_RATE = 'pump_micro_flow_rate_ml_s'
CONF_PUMP_MICRO_PWM = 'pump_micro_pwm_intensity'
CONF_PUMP_BLOOM_FLOW_RATE = 'pump_bloom_flow_rate_ml_s'
CONF_PUMP_BLOOM_PWM = 'pump_bloom_pwm_intensity'

# Configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ESPHomeHAL),
    cv.Required(CONF_SYSTEM_LED): cv.use_id(light.LightState),
    cv.Required(CONF_PH_SENSOR): cv.use_id(sensor.Sensor),
    cv.Required(CONF_PH_SENSOR_COMPONENT): cv.use_id(cg.PollingComponent),  # EZO pH UART component
    cv.Optional(CONF_LIGHT_SENSOR): cv.use_id(sensor.Sensor),
    cv.Optional(CONF_TEMPERATURE_SENSOR): cv.use_id(sensor.Sensor),
    cv.Optional(CONF_WATER_LEVEL_HIGH_SENSOR): cv.use_id(binary_sensor.BinarySensor),
    cv.Optional(CONF_WATER_LEVEL_LOW_SENSOR): cv.use_id(binary_sensor.BinarySensor),
    cv.Optional(CONF_WATER_LEVEL_EMPTY_SENSOR): cv.use_id(binary_sensor.BinarySensor),
    cv.Optional(CONF_TIME_SOURCE): cv.use_id(time.RealTimeClock),
    cv.Optional(CONF_PH_READING_INTERVAL, default='2h'): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_PH_MIN, default=5.5): cv.float_range(min=0.0, max=14.0),
    cv.Optional(CONF_PH_MAX, default=6.5): cv.float_range(min=0.0, max=14.0),

    # Actuator GPIO outputs (Phase 2: Hardware Control - 6 actuators)
    # NOTE: Using FloatOutput (LEDC PWM) for variable pump intensity control
    cv.Optional(CONF_MAG_VALVE_OUTPUT): cv.use_id(output.FloatOutput),
    cv.Optional(CONF_PUMP_PH_OUTPUT): cv.use_id(output.FloatOutput),
    cv.Optional(CONF_PUMP_GROW_OUTPUT): cv.use_id(output.FloatOutput),
    cv.Optional(CONF_PUMP_MICRO_OUTPUT): cv.use_id(output.FloatOutput),
    cv.Optional(CONF_PUMP_BLOOM_OUTPUT): cv.use_id(output.FloatOutput),
    cv.Optional(CONF_PUMP_WASTEWATER_OUTPUT): cv.use_id(output.FloatOutput),

    # Shelly HTTP switches (MVP: AirPump and WastewaterPump)
    cv.Optional(CONF_AIR_PUMP_SWITCH): cv.use_id(switch.Switch),
    cv.Optional(CONF_WASTEWATER_PUMP_SWITCH): cv.use_id(switch.Switch),
    cv.Optional(CONF_HTTP_REQUEST): cv.use_id(http_request.HttpRequestComponent),

    # Tank volume and valve configuration
    cv.Optional(CONF_TANK_VOLUME_DELTA_LITERS, default=10.0): cv.float_range(min=0.1, max=1000.0),
    cv.Optional(CONF_TOTAL_TANK_VOLUME_LITERS, default=10.0): cv.float_range(min=0.1, max=1000.0),
    cv.Optional(CONF_MAG_VALVE_FLOW_RATE, default=50.0): cv.float_range(min=0.001, max=1000.0),

    # Pump calibration configuration (flow rates and PWM intensities)
    cv.Optional(CONF_PUMP_PH_FLOW_RATE, default=1.0): cv.float_range(min=0.001, max=100.0),
    cv.Optional(CONF_PUMP_PH_PWM, default=1.0): cv.percentage,
    cv.Optional(CONF_PUMP_GROW_FLOW_RATE, default=1.0): cv.float_range(min=0.001, max=100.0),
    cv.Optional(CONF_PUMP_GROW_PWM, default=1.0): cv.percentage,
    cv.Optional(CONF_PUMP_MICRO_FLOW_RATE, default=1.0): cv.float_range(min=0.001, max=100.0),
    cv.Optional(CONF_PUMP_MICRO_PWM, default=1.0): cv.percentage,
    cv.Optional(CONF_PUMP_BLOOM_FLOW_RATE, default=1.0): cv.float_range(min=0.001, max=100.0),
    cv.Optional(CONF_PUMP_BLOOM_PWM, default=1.0): cv.percentage,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    """
    Code generation for PlantOS HAL component.

    Injects hardware dependencies (LED, sensors) into the HAL instance.
    """
    # Create HAL instance
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Inject LED dependency
    led = await cg.get_variable(config[CONF_SYSTEM_LED])
    cg.add(var.set_led(led))

    # Inject pH sensor dependency
    ph = await cg.get_variable(config[CONF_PH_SENSOR])
    cg.add(var.set_ph_sensor(ph))

    # Inject pH sensor component dependency (for calibration and direct readings)
    ph_component = await cg.get_variable(config[CONF_PH_SENSOR_COMPONENT])
    cg.add(var.set_ph_sensor_component(ph_component))

    # Inject light sensor dependency (optional)
    if CONF_LIGHT_SENSOR in config:
        light_sensor = await cg.get_variable(config[CONF_LIGHT_SENSOR])
        cg.add(var.set_light_sensor(light_sensor))

    # Inject temperature sensor dependency (optional)
    if CONF_TEMPERATURE_SENSOR in config:
        temperature_sensor = await cg.get_variable(config[CONF_TEMPERATURE_SENSOR])
        cg.add(var.set_temperature_sensor(temperature_sensor))

    # Inject water level sensor dependencies (optional)
    if CONF_WATER_LEVEL_HIGH_SENSOR in config:
        water_level_high = await cg.get_variable(config[CONF_WATER_LEVEL_HIGH_SENSOR])
        cg.add(var.set_water_level_high_sensor(water_level_high))

    if CONF_WATER_LEVEL_LOW_SENSOR in config:
        water_level_low = await cg.get_variable(config[CONF_WATER_LEVEL_LOW_SENSOR])
        cg.add(var.set_water_level_low_sensor(water_level_low))

    if CONF_WATER_LEVEL_EMPTY_SENSOR in config:
        water_level_empty = await cg.get_variable(config[CONF_WATER_LEVEL_EMPTY_SENSOR])
        cg.add(var.set_water_level_empty_sensor(water_level_empty))

    # Inject time source dependency (optional)
    if CONF_TIME_SOURCE in config:
        time_source = await cg.get_variable(config[CONF_TIME_SOURCE])
        cg.add(var.set_time_source(time_source))

    # Set pH reading interval configuration
    cg.add(var.set_ph_reading_interval(config[CONF_PH_READING_INTERVAL]))

    # Set pH range configuration
    cg.add(var.set_ph_range(config[CONF_PH_MIN], config[CONF_PH_MAX]))

    # Inject actuator GPIO outputs (Phase 2: Hardware Control)
    if CONF_MAG_VALVE_OUTPUT in config:
        mag_valve_out = await cg.get_variable(config[CONF_MAG_VALVE_OUTPUT])
        cg.add(var.set_mag_valve_output(mag_valve_out))

    if CONF_PUMP_PH_OUTPUT in config:
        pump_ph_out = await cg.get_variable(config[CONF_PUMP_PH_OUTPUT])
        cg.add(var.set_pump_ph_output(pump_ph_out))

    if CONF_PUMP_GROW_OUTPUT in config:
        pump_grow_out = await cg.get_variable(config[CONF_PUMP_GROW_OUTPUT])
        cg.add(var.set_pump_grow_output(pump_grow_out))

    if CONF_PUMP_MICRO_OUTPUT in config:
        pump_micro_out = await cg.get_variable(config[CONF_PUMP_MICRO_OUTPUT])
        cg.add(var.set_pump_micro_output(pump_micro_out))

    if CONF_PUMP_BLOOM_OUTPUT in config:
        pump_bloom_out = await cg.get_variable(config[CONF_PUMP_BLOOM_OUTPUT])
        cg.add(var.set_pump_bloom_output(pump_bloom_out))

    if CONF_PUMP_WASTEWATER_OUTPUT in config:
        pump_wastewater_out = await cg.get_variable(config[CONF_PUMP_WASTEWATER_OUTPUT])
        cg.add(var.set_pump_wastewater_output(pump_wastewater_out))

    # Inject Shelly HTTP switches (MVP: AirPump and WastewaterPump)
    if CONF_AIR_PUMP_SWITCH in config:
        air_pump_sw = await cg.get_variable(config[CONF_AIR_PUMP_SWITCH])
        cg.add(var.set_air_pump_switch(air_pump_sw))

    if CONF_WASTEWATER_PUMP_SWITCH in config:
        wastewater_pump_sw = await cg.get_variable(config[CONF_WASTEWATER_PUMP_SWITCH])
        cg.add(var.set_wastewater_pump_switch(wastewater_pump_sw))

    # Inject HTTP request component for direct Shelly control
    if CONF_HTTP_REQUEST in config:
        http_req = await cg.get_variable(config[CONF_HTTP_REQUEST])
        cg.add(var.set_http_request(http_req))

    # Inject tank volume and valve configuration
    cg.add(var.setTankVolumeDelta(config[CONF_TANK_VOLUME_DELTA_LITERS]))
    cg.add(var.setTotalTankVolume(config[CONF_TOTAL_TANK_VOLUME_LITERS]))
    cg.add(var.setMagValveFlowRate(config[CONF_MAG_VALVE_FLOW_RATE]))

    # Inject pump configurations (flow rates and PWM intensities)
    cg.add(var.setPumpConfig("AcidPump", config[CONF_PUMP_PH_FLOW_RATE], config[CONF_PUMP_PH_PWM]))
    cg.add(var.setPumpConfig("NutrientPumpA", config[CONF_PUMP_GROW_FLOW_RATE], config[CONF_PUMP_GROW_PWM]))
    cg.add(var.setPumpConfig("NutrientPumpB", config[CONF_PUMP_MICRO_FLOW_RATE], config[CONF_PUMP_MICRO_PWM]))
    cg.add(var.setPumpConfig("NutrientPumpC", config[CONF_PUMP_BLOOM_FLOW_RATE], config[CONF_PUMP_BLOOM_PWM]))

    # NOTE: pump_air_output removed - future Zigbee implementation
