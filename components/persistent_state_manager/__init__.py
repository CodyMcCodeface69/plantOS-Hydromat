"""
PersistentStateManager ESPHome Component

Manages critical event logging with NVS persistence for recovery
after power loss or unexpected reboots.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# Define the namespace and class
persistent_state_manager_ns = cg.esphome_ns.namespace('persistent_state_manager')
PersistentStateManager = persistent_state_manager_ns.class_('PersistentStateManager', cg.Component)

# Configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(PersistentStateManager),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    """Generate C++ code for this component"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
