"""
ActuatorSafetyGate ESPHome Component

Centralized safety layer for controlling all system actuators with
debouncing, duration limits, and comprehensive logging.

Phase 2: Updated to use HAL (Hardware Abstraction Layer) for actuator control.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# Define the namespace and class
actuator_safety_gate_ns = cg.esphome_ns.namespace('actuator_safety_gate')
ActuatorSafetyGate = actuator_safety_gate_ns.class_('ActuatorSafetyGate', cg.Component)

# Import HAL namespace (Phase 2)
plantos_hal_ns = cg.esphome_ns.namespace('plantos_hal')
HAL = plantos_hal_ns.class_('HAL')

# Configuration keys
CONF_HAL = 'hal'

# Configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ActuatorSafetyGate),
    cv.Required(CONF_HAL): cv.use_id(HAL),  # Phase 2: Require HAL dependency
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    """Generate C++ code for this component"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Phase 2: Inject HAL dependency
    hal = await cg.get_variable(config[CONF_HAL])
    cg.add(var.setHAL(hal))
