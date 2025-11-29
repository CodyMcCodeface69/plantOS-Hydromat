"""
time_logger: ESPHome Component for Periodic Time Logging

This component logs the current date and time to the serial monitor at a
configurable interval using non-blocking delays. It demonstrates:
- Integration with ESPHome's time component (NTP synchronization)
- Non-blocking periodic execution using millis()
- Formatted time output (dd.mm.yyyy HH:MM:SS)

ARCHITECTURE:
The time_logger component receives a reference to a time component (typically
configured for NTP) and periodically queries it for the current time. This
follows ESPHome's component linking pattern, allowing the logger to work with
any time platform (SNTP, GPS, RTC, etc.) without modification.

USAGE:
time_logger:
  id: my_logger
  time_source: sntp_time  # Reference to a time component
  log_interval: 5s        # How often to log (default: 5 seconds)
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import time
from esphome.const import CONF_ID
from esphome import automation

# Create a C++ namespace for the time_logger component
time_logger_ns = cg.esphome_ns.namespace('time_logger')

# Declare the C++ TimeLogger class inheriting from Component
# Component provides setup() and loop() lifecycle methods
TimeLogger = time_logger_ns.class_('TimeLogger', cg.Component)

# Configuration keys
CONF_TIME_SOURCE = 'time_source'
CONF_LOG_INTERVAL = 'log_interval'

# Define the YAML configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(TimeLogger),

    # time_source: Required reference to a time component
    # This allows the logger to access synchronized time data
    cv.Required(CONF_TIME_SOURCE): cv.use_id(time.RealTimeClock),

    # log_interval: Optional interval for logging (default: 5 seconds)
    # Accepts time strings like "5s", "10s", "1min", etc.
    cv.Optional(CONF_LOG_INTERVAL, default='5s'): cv.positive_time_period_milliseconds,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    """
    Code generation function that sets up the TimeLogger component.

    This generates C++ code to:
    1. Instantiate the TimeLogger object
    2. Register it as a component (enables setup()/loop() lifecycle)
    3. Inject the time component dependency
    4. Configure the logging interval
    """
    # Create the time_logger instance
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Retrieve the time component by ID and inject it
    # Generates: time_logger->set_time_source(sntp_time);
    time_source = await cg.get_variable(config[CONF_TIME_SOURCE])
    cg.add(var.set_time_source(time_source))

    # Set the logging interval in milliseconds
    # Generates: time_logger->set_log_interval(5000);
    cg.add(var.set_log_interval(config[CONF_LOG_INTERVAL]))
