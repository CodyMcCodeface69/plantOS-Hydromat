"""
Time Dummy Component

Provides a configurable dummy time source for testing calendar-based functionality.
Acts like SNTP time but uses configurable dummy data instead of NTP synchronization.

Features:
- Configurable initial date/time at boot
- Non-blocking clock that counts forward from initial time
- Manual time adjustment methods (add/subtract days/hours)
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.components import time as time_
from datetime import datetime

CODEOWNERS = ["@cody"]

# Define namespace
time_dummy_ns = cg.esphome_ns.namespace('time_dummy')

# Inherit from time::RealTimeClock
TimeDummy = time_dummy_ns.class_('TimeDummy', time_.RealTimeClock, cg.PollingComponent)

# Configuration keys
CONF_INITIAL_TIME = 'initial_time'

# Configuration schema
CONFIG_SCHEMA = time_.TIME_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(TimeDummy),
    cv.Required(CONF_INITIAL_TIME): cv.string,
}).extend(cv.polling_component_schema('1s'))

async def to_code(config):
    """Generate C++ code for TimeDummy component"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Parse initial time string (YYYY-MM-DD HH:MM:SS format)
    time_str = config[CONF_INITIAL_TIME]
    try:
        # Parse datetime string
        dt = datetime.strptime(time_str, "%Y-%m-%d %H:%M:%S")
        # Convert to Unix timestamp
        timestamp = int(dt.timestamp())
        cg.add(var.set_initial_timestamp(timestamp))
    except ValueError as e:
        raise cv.Invalid(
            f"Invalid time format '{time_str}'. "
            f"Use 'YYYY-MM-DD HH:MM:SS' format (e.g., '2025-12-30 08:00:00')"
        )
