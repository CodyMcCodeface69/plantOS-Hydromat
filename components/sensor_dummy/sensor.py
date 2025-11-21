import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID

dummy_ns = cg.esphome_ns.namespace('sensor_dummy')
SensorDummy = dummy_ns.class_('SensorDummy', sensor.Sensor, cg.PollingComponent)

CONFIG_SCHEMA = sensor.SENSOR_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(SensorDummy),
}).extend(cv.polling_component_schema('1s')) # Default polling 1s

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)
