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
CONF_MAX_DURATIONS = 'max_durations'
CONF_AIR_PUMP_CYCLING = 'air_pump_cycling'
CONF_ON_PERIOD = 'on_period'
CONF_OFF_PERIOD = 'off_period'

# Actuator IDs the gate knows (HAL / controller names)
ACTUATOR_IDS = ['WaterValve', 'AcidPump', 'NutrientPumpA', 'NutrientPumpB', 'NutrientPumpC',
                'AirPump', 'WastewaterPump', 'GrowLight']

# Configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ActuatorSafetyGate),
    cv.Required(CONF_HAL): cv.use_id(HAL),  # Phase 2: Require HAL dependency
    # Max ON duration per actuator (0s = no limit). Applied before any component setup.
    cv.Optional(CONF_MAX_DURATIONS, default={}): cv.Schema({
        cv.Optional(a): cv.positive_time_period_seconds for a in ACTUATOR_IDS
    }),
    # AirPump cycling periods set in setup(); cycling itself starts OFF (the
    # "Air Pump Cycling" switch turns it on).
    cv.Optional(CONF_AIR_PUMP_CYCLING): cv.Schema({
        cv.Required(CONF_ON_PERIOD): cv.positive_time_period_seconds,
        cv.Required(CONF_OFF_PERIOD): cv.positive_time_period_seconds,
    }),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    """Generate C++ code for this component"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Phase 2: Inject HAL dependency
    hal = await cg.get_variable(config[CONF_HAL])
    cg.add(var.setHAL(hal))

    for actuator, limit in config[CONF_MAX_DURATIONS].items():
        cg.add(var.setMaxDuration(actuator, limit.total_seconds))

    if CONF_AIR_PUMP_CYCLING in config:
        cycling = config[CONF_AIR_PUMP_CYCLING]
        cg.add(var.setBootCyclingPeriods("AirPump", cycling[CONF_ON_PERIOD].total_seconds,
                                         cycling[CONF_OFF_PERIOD].total_seconds))
