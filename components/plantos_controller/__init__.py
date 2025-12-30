"""
PlantOS Unified Controller Component

Unified controller managing system state, LED behaviors, and operation sequences.
Phase 8: Full Controller FSM with PSM integration and CentralStatusLogger ownership.
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

persistent_state_manager_ns = cg.esphome_ns.namespace('persistent_state_manager')
PersistentStateManager = persistent_state_manager_ns.class_('PersistentStateManager')

ezo_ph_uart_ns = cg.esphome_ns.namespace('ezo_ph_uart')
EZOPHUARTComponent = ezo_ph_uart_ns.class_('EZOPHUARTComponent')

calendar_manager_ns = cg.esphome_ns.namespace('calendar_manager')
CalendarManager = calendar_manager_ns.class_('CalendarManager')

# Configuration keys
CONF_HAL = 'hal'
CONF_SAFETY_GATE = 'safety_gate'
CONF_PERSISTENCE = 'persistence'
CONF_PH_SENSOR = 'ph_sensor'
CONF_CALENDAR = 'calendar'
CONF_ENABLE_STATUS_REPORTS = 'enable_status_reports'
CONF_STATUS_REPORT_INTERVAL = 'status_report_interval'
CONF_VERBOSE_MODE = 'verbose_mode'

# Configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(PlantOSController),
    cv.Required(CONF_HAL): cv.use_id(HAL),
    cv.Required(CONF_SAFETY_GATE): cv.use_id(ActuatorSafetyGate),
    cv.Optional(CONF_PERSISTENCE): cv.use_id(PersistentStateManager),
    cv.Optional(CONF_PH_SENSOR): cv.use_id(EZOPHUARTComponent),
    cv.Optional(CONF_CALENDAR): cv.use_id(CalendarManager),
    cv.Optional(CONF_ENABLE_STATUS_REPORTS, default=True): cv.boolean,
    cv.Optional(CONF_STATUS_REPORT_INTERVAL, default="30s"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_VERBOSE_MODE, default=False): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    """Generate C++ code for PlantOS Controller component"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Inject HAL dependency (required)
    hal = await cg.get_variable(config[CONF_HAL])
    cg.add(var.setHAL(hal))

    # Inject SafetyGate dependency (required)
    safety_gate = await cg.get_variable(config[CONF_SAFETY_GATE])
    cg.add(var.setSafetyGate(safety_gate))

    # Inject PersistentStateManager dependency (optional)
    if CONF_PERSISTENCE in config:
        psm = await cg.get_variable(config[CONF_PERSISTENCE])
        cg.add(var.setPersistenceManager(psm))

    # Inject pH Sensor dependency (optional, needed for calibration)
    if CONF_PH_SENSOR in config:
        ph_sensor = await cg.get_variable(config[CONF_PH_SENSOR])
        cg.add(var.setPhSensor(ph_sensor))

    # Inject Calendar Manager dependency (optional, needed for nutrient scheduling)
    if CONF_CALENDAR in config:
        calendar = await cg.get_variable(config[CONF_CALENDAR])
        cg.add(var.setCalendarManager(calendar))

    # Configure status logger settings
    cg.add(var.configureStatusLogger(
        config[CONF_ENABLE_STATUS_REPORTS],
        config[CONF_STATUS_REPORT_INTERVAL],
        config[CONF_VERBOSE_MODE]
    ))
