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

# Configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ESPHomeHAL),
    cv.Required(CONF_SYSTEM_LED): cv.use_id(light.LightState),
    cv.Required(CONF_PH_SENSOR): cv.use_id(sensor.Sensor),
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

    # TODO: Inject GPIO/PWM outputs for pumps/valves (Phase 2)
    # Example:
    # if CONF_ACID_PUMP in config:
    #     acid_pump = await cg.get_variable(config[CONF_ACID_PUMP])
    #     cg.add(var.set_acid_pump(acid_pump))
