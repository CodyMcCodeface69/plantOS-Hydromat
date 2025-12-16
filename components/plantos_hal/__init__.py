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

# Configuration keys
CONF_SYSTEM_LED = 'system_led'
CONF_PH_SENSOR = 'ph_sensor'
CONF_PH_SENSOR_COMPONENT = 'ph_sensor_component'
CONF_LIGHT_SENSOR = 'light_sensor'
CONF_TEMPERATURE_SENSOR = 'temperature_sensor'
CONF_PH_READING_INTERVAL = 'ph_reading_interval'
CONF_PH_MIN = 'ph_min'
CONF_PH_MAX = 'ph_max'

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
    # TODO: Add GPIO/PWM outputs for pumps/valves in Phase 2
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

    # TODO: Inject GPIO/PWM outputs for pumps/valves (Phase 2)
    # Example:
    # if CONF_ACID_PUMP in config:
    #     acid_pump = await cg.get_variable(config[CONF_ACID_PUMP])
    #     cg.add(var.set_acid_pump(acid_pump))
