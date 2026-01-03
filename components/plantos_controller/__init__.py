"""
PlantOS Unified Controller Component

Unified controller managing system state, LED behaviors, and operation sequences.
Phase 8: Full Controller FSM with PSM integration and CentralStatusLogger ownership.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from datetime import datetime

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

time_ns = cg.esphome_ns.namespace('time')
RealTimeClock = time_ns.class_('RealTimeClock')

# Configuration keys
CONF_HAL = 'hal'
CONF_SAFETY_GATE = 'safety_gate'
CONF_PERSISTENCE = 'persistence'
CONF_PH_SENSOR = 'ph_sensor'
CONF_CALENDAR = 'calendar'
CONF_TIME_SOURCE = 'time_source'
CONF_GROW_START_DATE = 'grow_start_date'
CONF_ENABLE_STATUS_REPORTS = 'enable_status_reports'
CONF_STATUS_REPORT_INTERVAL = 'status_report_interval'
CONF_VERBOSE_MODE = 'verbose_mode'
CONF_420_MODE = 'enable_420_mode'
CONF_NIGHT_MODE_ENABLED = 'night_mode_enabled'
CONF_NIGHT_MODE_START_HOUR = 'night_mode_start_hour'
CONF_NIGHT_MODE_END_HOUR = 'night_mode_end_hour'

# Configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(PlantOSController),
    cv.Required(CONF_HAL): cv.use_id(HAL),
    cv.Required(CONF_SAFETY_GATE): cv.use_id(ActuatorSafetyGate),
    cv.Optional(CONF_PERSISTENCE): cv.use_id(PersistentStateManager),
    cv.Optional(CONF_PH_SENSOR): cv.use_id(EZOPHUARTComponent),
    cv.Optional(CONF_CALENDAR): cv.use_id(CalendarManager),
    cv.Optional(CONF_TIME_SOURCE): cv.use_id(RealTimeClock),
    cv.Optional(CONF_GROW_START_DATE): cv.string,
    cv.Optional(CONF_ENABLE_STATUS_REPORTS, default=True): cv.boolean,
    cv.Optional(CONF_STATUS_REPORT_INTERVAL, default="30s"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_VERBOSE_MODE, default=False): cv.boolean,
    cv.Optional(CONF_420_MODE, default=True): cv.boolean,
    cv.Optional(CONF_NIGHT_MODE_ENABLED, default=False): cv.boolean,
    cv.Optional(CONF_NIGHT_MODE_START_HOUR, default=22): cv.int_range(min=0, max=23),
    cv.Optional(CONF_NIGHT_MODE_END_HOUR, default=8): cv.int_range(min=0, max=23),
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

    # Inject Time Source dependency (optional, needed for automatic day calculation)
    if CONF_TIME_SOURCE in config:
        time_source = await cg.get_variable(config[CONF_TIME_SOURCE])
        cg.add(var.setTimeSource(time_source))

    # Configure grow start date (optional, enables automatic day calculation)
    if CONF_GROW_START_DATE in config:
        # Parse date string (YYYY-MM-DD format) to Unix timestamp
        date_str = config[CONF_GROW_START_DATE]
        try:
            # Parse YYYY-MM-DD format
            dt = datetime.strptime(date_str, "%Y-%m-%d")
            # Get timestamp (seconds since epoch at midnight UTC)
            timestamp = int(dt.timestamp())
            cg.add(var.setGrowStartDate(timestamp))
        except ValueError as e:
            raise cv.Invalid(f"Invalid date format '{date_str}'. Use YYYY-MM-DD format (e.g., '2025-12-30')")

    # Configure status logger settings
    cg.add(var.configureStatusLogger(
        config[CONF_ENABLE_STATUS_REPORTS],
        config[CONF_STATUS_REPORT_INTERVAL],
        config[CONF_VERBOSE_MODE],
        config[CONF_420_MODE]
    ))

    # Configure night mode settings
    cg.add(var.setNightModeConfig(
        config[CONF_NIGHT_MODE_ENABLED],
        config[CONF_NIGHT_MODE_START_HOUR],
        config[CONF_NIGHT_MODE_END_HOUR]
    ))
