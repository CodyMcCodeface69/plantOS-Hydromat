"""
PlantOS Unified Controller Component

Unified controller managing system state, LED behaviors, and operation sequences.
Phase 4: Full Controller FSM implementation with HAL and SafetyGate integration.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# Define namespaces
plantos_controller_ns = cg.esphome_ns.namespace('plantos_controller')
PlantOSController = plantos_controller_ns.class_('PlantOSController', cg.Component)

# Import dependencies
plantos_hal_ns = cg.esphome_ns.namespace('plantos_hal')
HAL = plantos_hal_ns.class_('HAL')

actuator_safety_gate_ns = cg.esphome_ns.namespace('actuator_safety_gate')
ActuatorSafetyGate = actuator_safety_gate_ns.class_('ActuatorSafetyGate')

# Configuration keys
CONF_HAL = 'hal'
CONF_SAFETY_GATE = 'safety_gate'

# Configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(PlantOSController),
    cv.Required(CONF_HAL): cv.use_id(HAL),
    cv.Required(CONF_SAFETY_GATE): cv.use_id(ActuatorSafetyGate),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    """Generate C++ code for PlantOS Controller component"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Inject HAL dependency
    hal = await cg.get_variable(config[CONF_HAL])
    cg.add(var.setHAL(hal))

    # Inject SafetyGate dependency
    safety_gate = await cg.get_variable(config[CONF_SAFETY_GATE])
    cg.add(var.setSafetyGate(safety_gate))
