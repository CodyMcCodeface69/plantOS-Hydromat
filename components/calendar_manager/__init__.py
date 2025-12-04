"""
CalendarManager ESPHome Component

Manages the daily schedule and current day for the PlantOS grow cycle.
Stores target pH values and nutrient dosing durations for each of the 120 days.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# Configuration keys
CONF_SCHEDULE_JSON = "schedule_json"
CONF_SAFE_MODE = "safe_mode"
CONF_VERBOSE = "verbose"
CONF_STATUS_LOG_INTERVAL = "status_log_interval"

# Define the namespace and class
calendar_manager_ns = cg.esphome_ns.namespace('calendar_manager')
CalendarManager = calendar_manager_ns.class_('CalendarManager', cg.Component)

# Configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(CalendarManager),
    cv.Required(CONF_SCHEDULE_JSON): cv.string,
    cv.Optional(CONF_SAFE_MODE, default=False): cv.boolean,
    cv.Optional(CONF_VERBOSE, default=False): cv.boolean,
    cv.Optional(CONF_STATUS_LOG_INTERVAL, default="30s"): cv.positive_time_period_milliseconds,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    """Generate C++ code for this component"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set configuration parameters
    cg.add(var.set_schedule_json(config[CONF_SCHEDULE_JSON]))
    cg.add(var.set_safe_mode(config[CONF_SAFE_MODE]))
    cg.add(var.set_verbose(config[CONF_VERBOSE]))
    cg.add(var.set_status_log_interval(config[CONF_STATUS_LOG_INTERVAL]))
