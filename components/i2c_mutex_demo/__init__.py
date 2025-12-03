import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# Define the component namespace
i2c_mutex_demo_ns = cg.esphome_ns.namespace('i2c_mutex_demo')
I2CMutexDemo = i2c_mutex_demo_ns.class_('I2CMutexDemo', cg.Component)

# Configuration keys
CONF_TEST_MODE = 'test_mode'

# Configuration schema for the component
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(I2CMutexDemo),
    cv.Optional(CONF_TEST_MODE, default=False): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    """Generate C++ code for the component."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set test mode flag
    cg.add(var.set_test_mode(config[CONF_TEST_MODE]))
