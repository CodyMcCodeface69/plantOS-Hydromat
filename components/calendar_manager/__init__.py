"""
CalendarManager ESPHome Component

Grow calendars (Anzucht, Mutter, Haupt) as phase tables and the per-calendar grow day.
The `calendars:` block is validated and turned into C++ by phase_tables.py (plain
Python, also used by the host tests); the tables are emitted into main.cpp as
`static const` data (flash) and handed to the component by pointer.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

from .phase_tables import TABLE_SYMBOL, CalendarError, render_cpp, validate_calendars

CONF_CALENDARS = "calendars"
CONF_SAFE_MODE = "safe_mode"
CONF_VERBOSE = "verbose"
CONF_STATUS_LOG_INTERVAL = "status_log_interval"

calendar_manager_ns = cg.esphome_ns.namespace("calendar_manager")
CalendarManager = calendar_manager_ns.class_("CalendarManager", cg.Component)


def _validate_calendars(value):
    try:
        return validate_calendars(value)
    except CalendarError as err:
        raise cv.Invalid(str(err)) from err


CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(CalendarManager),
    cv.Required(CONF_CALENDARS): _validate_calendars,
    cv.Optional(CONF_SAFE_MODE, default=False): cv.boolean,
    cv.Optional(CONF_VERBOSE, default=False): cv.boolean,
    cv.Optional(CONF_STATUS_LOG_INTERVAL, default="30s"): cv.positive_time_period_milliseconds,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # File-scope definitions in main.cpp, after `#include "esphome.h"` (which includes
    # phase_table.h through CalendarManager.h).
    cg.add_global(cg.RawStatement(render_cpp(config[CONF_CALENDARS])))
    cg.add(var.set_calendars(cg.RawExpression(TABLE_SYMBOL)))
    cg.add(var.set_safe_mode(config[CONF_SAFE_MODE]))
    cg.add(var.set_verbose(config[CONF_VERBOSE]))
    cg.add(var.set_status_log_interval(config[CONF_STATUS_LOG_INTERVAL]))
