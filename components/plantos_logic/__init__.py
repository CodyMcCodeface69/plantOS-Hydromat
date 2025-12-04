"""
PlantOSLogic ESPHome Component

Main application logic FSM for routine orchestration (pH correction, feeding, water management).
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, text_sensor
from esphome.const import CONF_ID

# Import other PlantOS component namespaces
actuator_safety_gate_ns = cg.esphome_ns.namespace('actuator_safety_gate')
ActuatorSafetyGate = actuator_safety_gate_ns.class_('ActuatorSafetyGate')

persistent_state_manager_ns = cg.esphome_ns.namespace('persistent_state_manager')
PersistentStateManager = persistent_state_manager_ns.class_('PersistentStateManager')

calendar_manager_ns = cg.esphome_ns.namespace('calendar_manager')
CalendarManager = calendar_manager_ns.class_('CalendarManager')

# Define the namespace and class
plantos_logic_ns = cg.esphome_ns.namespace('plantos_logic')
PlantOSLogic = plantos_logic_ns.class_('PlantOSLogic', cg.Component)

# CentralStatusLogger is used by pointer (forward declaration)
# We don't need to import it since it's handled as a raw pointer in C++

# Configuration keys
CONF_SAFETY_GATE = "safety_gate"
CONF_PSM = "psm"
CONF_STATUS_LOGGER = "status_logger"
CONF_CALENDAR = "calendar"
CONF_PH_SENSOR = "ph_sensor"
CONF_STATE_TEXT = "state_text"

# Configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(PlantOSLogic),

    # Required dependencies
    cv.Required(CONF_SAFETY_GATE): cv.use_id(ActuatorSafetyGate),
    cv.Required(CONF_PSM): cv.use_id(PersistentStateManager),
    cv.Required(CONF_CALENDAR): cv.use_id(CalendarManager),

    # Optional dependencies
    cv.Optional(CONF_PH_SENSOR): cv.use_id(sensor.Sensor),
    cv.Optional(CONF_STATE_TEXT): cv.use_id(text_sensor.TextSensor),

    # Note: status_logger is injected manually via raw pointer
    # (not validated by cv.use_id because it's accessed via controller)
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    """Generate C++ code for this component"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Inject safety gate
    safety_gate = await cg.get_variable(config[CONF_SAFETY_GATE])
    cg.add(var.set_safety_gate(safety_gate))

    # Inject PSM
    psm = await cg.get_variable(config[CONF_PSM])
    cg.add(var.set_psm(psm))

    # Inject calendar
    calendar = await cg.get_variable(config[CONF_CALENDAR])
    cg.add(var.set_calendar(calendar))

    # Inject pH sensor (optional)
    if CONF_PH_SENSOR in config:
        ph_sensor = await cg.get_variable(config[CONF_PH_SENSOR])
        cg.add(var.set_ph_sensor(ph_sensor))

    # Inject state text sensor (optional)
    if CONF_STATE_TEXT in config:
        state_text = await cg.get_variable(config[CONF_STATE_TEXT])
        cg.add(var.set_state_text(state_text))
