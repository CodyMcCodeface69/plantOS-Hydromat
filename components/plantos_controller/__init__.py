"""
PlantOS Unified Controller Component

Unified controller managing system state, LED behaviors, and operation sequences.
Phase 8: Full Controller FSM with PSM integration and CentralStatusLogger ownership.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
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

calendar_manager_ns = cg.esphome_ns.namespace('calendar_manager')
CalendarManager = calendar_manager_ns.class_('CalendarManager')

time_ns = cg.esphome_ns.namespace('time')
RealTimeClock = time_ns.class_('RealTimeClock')

alert_service_ns = cg.global_ns.namespace('alert_service')
AlertService = alert_service_ns.class_('AlertService')

# Configuration keys
CONF_HAL = 'hal'
CONF_SAFETY_GATE = 'safety_gate'
CONF_PERSISTENCE = 'persistence'
CONF_CALENDAR = 'calendar'
CONF_TIME_SOURCE = 'time_source'
CONF_ENABLE_STATUS_REPORTS = 'enable_status_reports'
CONF_STATUS_REPORT_INTERVAL = 'status_report_interval'
CONF_VERBOSE_MODE = 'verbose_mode'
CONF_420_MODE = 'enable_420_mode'
CONF_NIGHT_MODE_ENABLED = 'night_mode_enabled'
CONF_NIGHT_MODE_START_HOUR = 'night_mode_start_hour'
CONF_NIGHT_MODE_END_HOUR = 'night_mode_end_hour'
CONF_ALERT_SERVICE = 'alert_service'
CONF_PH_K_FACTOR = 'ph_k_factor'
CONF_PH_K_FACTOR_NUMBER = 'ph_k_factor_number'
CONF_PRESETS = 'presets'
CONF_TANK_VOLUME_DELTA = 'tank_volume_delta_liters'
CONF_TANK_VOLUME_TOTAL = 'total_tank_volume_liters'
CONF_FUNCTIONS = 'functions'
CONF_NO_AIR_PUMP_SETTLE_FACTOR = 'no_air_pump_settle_factor'

# Preset index = calendar index (preset_model.h, calendar_manager CAL_*)
PRESET_INDEX = {'anzucht': 0, 'mutter': 1, 'haupt': 2}
# Function bits (preset_model.h FN_*)
FUNCTION_BITS = {'nutrients': 1, 'water_fill': 2, 'drain': 4, 'air_pump': 8, 'grow_light': 16}
VOLUME_MIN_L = 1.0
VOLUME_MAX_L = 300.0


def _validate_preset(value):
    if value[CONF_TANK_VOLUME_DELTA] > value[CONF_TANK_VOLUME_TOTAL]:
        raise cv.Invalid(f"{CONF_TANK_VOLUME_DELTA} must not be larger than {CONF_TANK_VOLUME_TOTAL}")
    return value


PRESET_SCHEMA = cv.All(cv.Schema({
    cv.Required(CONF_TANK_VOLUME_DELTA): cv.float_range(min=VOLUME_MIN_L, max=VOLUME_MAX_L),
    cv.Required(CONF_TANK_VOLUME_TOTAL): cv.float_range(min=VOLUME_MIN_L, max=VOLUME_MAX_L),
    cv.Required(CONF_FUNCTIONS): cv.ensure_list(cv.one_of(*FUNCTION_BITS, lower=True)),
}), _validate_preset)

# Without a `presets:` block (or a preset in it) the built-in defaults of preset_model.h apply.
PRESETS_SCHEMA = cv.Schema({cv.Optional(name): PRESET_SCHEMA for name in PRESET_INDEX})

# Configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(PlantOSController),
    cv.Required(CONF_HAL): cv.use_id(HAL),
    cv.Required(CONF_SAFETY_GATE): cv.use_id(ActuatorSafetyGate),
    cv.Optional(CONF_PERSISTENCE): cv.use_id(PersistentStateManager),
    cv.Optional(CONF_CALENDAR): cv.use_id(CalendarManager),
    cv.Optional(CONF_TIME_SOURCE): cv.use_id(RealTimeClock),
    cv.Optional(CONF_ENABLE_STATUS_REPORTS, default=True): cv.boolean,
    cv.Optional(CONF_STATUS_REPORT_INTERVAL, default="30s"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_VERBOSE_MODE, default=False): cv.boolean,
    cv.Optional(CONF_420_MODE, default=True): cv.boolean,
    cv.Optional(CONF_NIGHT_MODE_ENABLED, default=False): cv.boolean,
    cv.Optional(CONF_NIGHT_MODE_START_HOUR, default=22): cv.int_range(min=0, max=23),
    cv.Optional(CONF_NIGHT_MODE_END_HOUR, default=8): cv.int_range(min=0, max=23),
    cv.Optional(CONF_ALERT_SERVICE): cv.use_id(AlertService),
    cv.Optional(CONF_PH_K_FACTOR, default=0.25): cv.float_range(min=0.01, max=0.50),
    cv.Optional(CONF_PH_K_FACTOR_NUMBER): cv.use_id(number.Number),
    cv.Optional(CONF_PRESETS, default={}): PRESETS_SCHEMA,
    cv.Optional(CONF_NO_AIR_PUMP_SETTLE_FACTOR, default=2.0): cv.float_range(min=1.0, max=5.0),
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

    # Inject Calendar Manager dependency (optional, needed for nutrient scheduling)
    if CONF_CALENDAR in config:
        calendar = await cg.get_variable(config[CONF_CALENDAR])
        cg.add(var.setCalendarManager(calendar))

    # Inject Time Source dependency (optional, needed for automatic day calculation)
    if CONF_TIME_SOURCE in config:
        time_source = await cg.get_variable(config[CONF_TIME_SOURCE])
        cg.add(var.setTimeSource(time_source))

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

    # Inject AlertService dependency (optional)
    if CONF_ALERT_SERVICE in config:
        alert_svc = await cg.get_variable(config[CONF_ALERT_SERVICE])
        cg.add(var.setAlertService(alert_svc))

    # pH K-factor (acid dose aggressiveness)
    cg.add(var.set_ph_k_factor(config[CONF_PH_K_FACTOR]))

    # Number entity that shows EMA K-factor updates (only after a correction cycle)
    if CONF_PH_K_FACTOR_NUMBER in config:
        k_number = await cg.get_variable(config[CONF_PH_K_FACTOR_NUMBER])
        cg.add(var.set_ph_k_number(k_number))

    # Preset defaults (YAML); the NVS store of the controller overrides a preset once it was
    # edited in the web UI
    for name, preset in config[CONF_PRESETS].items():
        mask = 0
        for fn in preset[CONF_FUNCTIONS]:
            mask |= FUNCTION_BITS[fn]
        cg.add(var.setPresetDefaults(PRESET_INDEX[name], preset[CONF_TANK_VOLUME_DELTA],
                                     preset[CONF_TANK_VOLUME_TOTAL], mask))
    cg.add(var.setNoAirPumpSettleFactor(config[CONF_NO_AIR_PUMP_SETTLE_FACTOR]))
