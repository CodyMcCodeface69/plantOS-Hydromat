import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, light
from esphome.const import CONF_ID

# Define namespace and class
controller_ns = cg.esphome_ns.namespace('controller')
Controller = controller_ns.class_('Controller', cg.Component)

# Configuration keys
CONF_SENSOR_SOURCE = 'sensor_source'
CONF_LIGHT_TARGET = 'light_target'

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Controller),
    cv.Required(CONF_SENSOR_SOURCE): cv.use_id(sensor.Sensor),
    cv.Required(CONF_LIGHT_TARGET): cv.use_id(light.LightState),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Link the sensor object (Source)
    sens = await cg.get_variable(config[CONF_SENSOR_SOURCE])
    cg.add(var.set_sensor_source(sens))

    # Link the light object (Target)
    lit = await cg.get_variable(config[CONF_LIGHT_TARGET])
    cg.add(var.set_light_target(lit))
