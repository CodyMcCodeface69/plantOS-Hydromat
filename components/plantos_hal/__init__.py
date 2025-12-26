"""
PlantOS Hardware Abstraction Layer (HAL) Component

ESPHome Python integration for the HAL component.
Provides platform-agnostic hardware interface for the unified Controller.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, sensor
from esphome.const import (
    CONF_ID,
)

# Define namespace and classes
plantos_hal_ns = cg.esphome_ns.namespace('plantos_hal')
HAL = plantos_hal_ns.class_('HAL')
ESPHomeHAL = plantos_hal_ns.class_('ESPHomeHAL', HAL, cg.Component)

# Import switch component for actuator control
from esphome.components import switch

# Configuration keys
CONF_SYSTEM_LED = 'system_led'
CONF_PH_SENSOR = 'ph_sensor'
CONF_PH_SENSOR_COMPONENT = 'ph_sensor_component'
CONF_LIGHT_SENSOR = 'light_sensor'
CONF_TEMPERATURE_SENSOR = 'temperature_sensor'
CONF_PH_READING_INTERVAL = 'ph_reading_interval'
CONF_PH_MIN = 'ph_min'
CONF_PH_MAX = 'ph_max'

# Actuator switch keys (Phase 2 - All 7 actuators)
CONF_MAG_VALVE_SWITCH = 'mag_valve_switch'
CONF_PUMP_PH_SWITCH = 'pump_ph_switch'
CONF_PUMP_GROW_SWITCH = 'pump_grow_switch'
CONF_PUMP_MICRO_SWITCH = 'pump_micro_switch'
CONF_PUMP_BLOOM_SWITCH = 'pump_bloom_switch'
CONF_PUMP_WASTEWATER_SWITCH = 'pump_wastewater_switch'
CONF_PUMP_AIR_SWITCH = 'pump_air_switch'

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

    # Actuator switches (Phase 2: Hardware Control - All 7 actuators)
    cv.Optional(CONF_MAG_VALVE_SWITCH): cv.use_id(switch.Switch),
    cv.Optional(CONF_PUMP_PH_SWITCH): cv.use_id(switch.Switch),
    cv.Optional(CONF_PUMP_GROW_SWITCH): cv.use_id(switch.Switch),
    cv.Optional(CONF_PUMP_MICRO_SWITCH): cv.use_id(switch.Switch),
    cv.Optional(CONF_PUMP_BLOOM_SWITCH): cv.use_id(switch.Switch),
    cv.Optional(CONF_PUMP_WASTEWATER_SWITCH): cv.use_id(switch.Switch),
    cv.Optional(CONF_PUMP_AIR_SWITCH): cv.use_id(switch.Switch),
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

    # Inject actuator switches (Phase 2: Hardware Control)
    if CONF_MAG_VALVE_SWITCH in config:
        mag_valve_sw = await cg.get_variable(config[CONF_MAG_VALVE_SWITCH])
        cg.add(var.set_mag_valve_switch(mag_valve_sw))

    if CONF_PUMP_PH_SWITCH in config:
        pump_ph_sw = await cg.get_variable(config[CONF_PUMP_PH_SWITCH])
        cg.add(var.set_pump_ph_switch(pump_ph_sw))

    if CONF_PUMP_GROW_SWITCH in config:
        pump_grow_sw = await cg.get_variable(config[CONF_PUMP_GROW_SWITCH])
        cg.add(var.set_pump_grow_switch(pump_grow_sw))

    if CONF_PUMP_MICRO_SWITCH in config:
        pump_micro_sw = await cg.get_variable(config[CONF_PUMP_MICRO_SWITCH])
        cg.add(var.set_pump_micro_switch(pump_micro_sw))

    if CONF_PUMP_BLOOM_SWITCH in config:
        pump_bloom_sw = await cg.get_variable(config[CONF_PUMP_BLOOM_SWITCH])
        cg.add(var.set_pump_bloom_switch(pump_bloom_sw))

    if CONF_PUMP_WASTEWATER_SWITCH in config:
        pump_wastewater_sw = await cg.get_variable(config[CONF_PUMP_WASTEWATER_SWITCH])
        cg.add(var.set_pump_wastewater_switch(pump_wastewater_sw))

    if CONF_PUMP_AIR_SWITCH in config:
        pump_air_sw = await cg.get_variable(config[CONF_PUMP_AIR_SWITCH])
        cg.add(var.set_pump_air_switch(pump_air_sw))
