"""
PlantOS Hardware Abstraction Layer (HAL) Component

ESPHome Python integration for the HAL component.
Provides platform-agnostic hardware interface for the unified Controller.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, sensor, output
from esphome.const import (
    CONF_ID,
)

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
CONF_PH_READING_INTERVAL = 'ph_reading_interval'
CONF_PH_MIN = 'ph_min'
CONF_PH_MAX = 'ph_max'

# Actuator GPIO output keys (Phase 2 - 6 actuators)
# NOTE: Using outputs instead of switches to avoid circular dependency
# NOTE: Air pump removed - future Zigbee implementation
CONF_MAG_VALVE_OUTPUT = 'mag_valve_output'
CONF_PUMP_PH_OUTPUT = 'pump_ph_output'
CONF_PUMP_GROW_OUTPUT = 'pump_grow_output'
CONF_PUMP_MICRO_OUTPUT = 'pump_micro_output'
CONF_PUMP_BLOOM_OUTPUT = 'pump_bloom_output'
CONF_PUMP_WASTEWATER_OUTPUT = 'pump_wastewater_output'

# Configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ESPHomeHAL),
    cv.Required(CONF_SYSTEM_LED): cv.use_id(light.LightState),
    cv.Required(CONF_PH_SENSOR): cv.use_id(sensor.Sensor),
    cv.Required(CONF_PH_SENSOR_COMPONENT): cv.use_id(cg.PollingComponent),  # EZO pH UART component
    cv.Optional(CONF_LIGHT_SENSOR): cv.use_id(sensor.Sensor),
    cv.Optional(CONF_TEMPERATURE_SENSOR): cv.use_id(sensor.Sensor),
    cv.Optional(CONF_PH_READING_INTERVAL, default='2h'): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_PH_MIN, default=5.5): cv.float_range(min=0.0, max=14.0),
    cv.Optional(CONF_PH_MAX, default=6.5): cv.float_range(min=0.0, max=14.0),

    # Actuator GPIO outputs (Phase 2: Hardware Control - 6 actuators)
    # NOTE: Using outputs instead of switches to avoid circular dependency
    # NOTE: Air pump removed - future Zigbee implementation
    cv.Optional(CONF_MAG_VALVE_OUTPUT): cv.use_id(output.BinaryOutput),
    cv.Optional(CONF_PUMP_PH_OUTPUT): cv.use_id(output.BinaryOutput),
    cv.Optional(CONF_PUMP_GROW_OUTPUT): cv.use_id(output.BinaryOutput),
    cv.Optional(CONF_PUMP_MICRO_OUTPUT): cv.use_id(output.BinaryOutput),
    cv.Optional(CONF_PUMP_BLOOM_OUTPUT): cv.use_id(output.BinaryOutput),
    cv.Optional(CONF_PUMP_WASTEWATER_OUTPUT): cv.use_id(output.BinaryOutput),
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

    # NOTE: pump_air_output removed - future Zigbee implementation
