"""
PSMChecker ESPHome Component

Test component for validating Persistent State Manager recovery
after power loss or unexpected reboot.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# Import the PSM and Controller namespaces
persistent_state_manager_ns = cg.esphome_ns.namespace('persistent_state_manager')
PersistentStateManager = persistent_state_manager_ns.class_('PersistentStateManager')

controller_ns = cg.esphome_ns.namespace('controller')
Controller = controller_ns.class_('Controller')

# Define our namespace and class
psm_checker_ns = cg.esphome_ns.namespace('psm_checker')
PSMChecker = psm_checker_ns.class_('PSMChecker', cg.Component)

# Configuration constants
CONF_PSM = 'psm'
CONF_CONTROLLER = 'controller'
CONF_TEST_INTERVAL = 'test_interval'

# Configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(PSMChecker),
    cv.Required(CONF_PSM): cv.use_id(PersistentStateManager),
    cv.Optional(CONF_CONTROLLER): cv.use_id(Controller),
    cv.Optional(CONF_TEST_INTERVAL, default='0s'): cv.positive_time_period_milliseconds,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    """Generate C++ code for this component"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Link to PSM
    psm = await cg.get_variable(config[CONF_PSM])
    cg.add(var.set_psm(psm))

    # Link to controller if specified
    if CONF_CONTROLLER in config:
        controller = await cg.get_variable(config[CONF_CONTROLLER])
        cg.add(var.set_controller(controller))

    # Set test interval
    cg.add(var.set_test_interval(config[CONF_TEST_INTERVAL]))
