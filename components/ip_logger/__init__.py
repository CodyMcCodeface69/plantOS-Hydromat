"""
ip_logger: ESPHome Component for Periodic IP Address Logging

This component logs the ESP32's current local IP address to the serial monitor
at a configurable interval (default: 30 seconds) using non-blocking delays.

FEATURES:
- Retrieves IP address from WiFi connection
- Only logs when WiFi is connected (not when disconnected or connecting)
- Non-blocking periodic execution using millis()
- Formatted output: "Current IP: 192.168.1.100"

ARCHITECTURE:
The ip_logger component accesses the WiFi component to retrieve the current
IP address and connection status. This follows ESPHome's component integration
pattern, allowing monitoring of network connectivity status.

USAGE:
ip_logger:
  id: my_ip_logger
  log_interval: 30s  # How often to log IP (default: 30 seconds)
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# Create a C++ namespace for the ip_logger component
ip_logger_ns = cg.esphome_ns.namespace('ip_logger')

# Declare the C++ IPLogger class inheriting from Component
# Component provides setup() and loop() lifecycle methods
IPLogger = ip_logger_ns.class_('IPLogger', cg.Component)

# Configuration keys
CONF_LOG_INTERVAL = 'log_interval'

# Define the YAML configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(IPLogger),

    # log_interval: Optional interval for logging (default: 30 seconds)
    # Accepts time strings like "30s", "1min", "60s", etc.
    cv.Optional(CONF_LOG_INTERVAL, default='30s'): cv.positive_time_period_milliseconds,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    """
    Code generation function that sets up the IPLogger component.

    This generates C++ code to:
    1. Instantiate the IPLogger object
    2. Register it as a component (enables setup()/loop() lifecycle)
    3. Configure the logging interval
    """
    # Create the ip_logger instance
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set the logging interval in milliseconds
    # Generates: ip_logger->set_log_interval(30000);
    cg.add(var.set_log_interval(config[CONF_LOG_INTERVAL]))
