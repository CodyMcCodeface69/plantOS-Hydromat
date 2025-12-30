"""
Time Dummy Platform for ESPHome Time Component

This file is required for ESPHome to recognize time_dummy as a time platform.
"""
from esphome.components import time
import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import CONF_ID
from datetime import datetime

time_dummy_ns = cg.esphome_ns.namespace('time_dummy')
TimeDummy = time_dummy_ns.class_('TimeDummy', time.RealTimeClock)

CONF_INITIAL_TIME = 'initial_time'

CONFIG_SCHEMA = time.TIME_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(TimeDummy),
    cv.Required(CONF_INITIAL_TIME): cv.string,
})

async def to_code(config):
    """Generate C++ code for TimeDummy component"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await time.register_time(var, config)

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
