"""
wdt_manager: ESPHome Component for Hardware Watchdog Timer Management

This component implements a hardware watchdog timer (WDT) that monitors system
health and automatically reboots the device if it becomes unresponsive.

Features:
- Hardware WDT initialization with configurable timeout (default: 10 seconds)
- Regular WDT feeding to indicate healthy operation
- Simulated crash mode for testing WDT functionality
- Non-blocking periodic execution using millis()

ARCHITECTURE:
The WDT Manager uses ESP-IDF's Task Watchdog Timer (TWDT) API to configure
and manage the hardware watchdog. It feeds the watchdog every 500ms during
normal operation, and can simulate a crash by stopping the feeding process.

USAGE:
wdt_manager:
  id: my_wdt
  timeout: 10s           # WDT timeout (default: 10 seconds)
  feed_interval: 500ms   # How often to feed WDT (default: 500ms)
  test_mode: true        # Enable crash simulation (default: false)
  crash_delay: 20s       # Time before simulated crash (default: 20 seconds)
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# Create a C++ namespace for the wdt_manager component
wdt_manager_ns = cg.esphome_ns.namespace('wdt_manager')

# Declare the C++ WDTManager class inheriting from Component
WDTManager = wdt_manager_ns.class_('WDTManager', cg.Component)

# Configuration keys
CONF_TIMEOUT = 'timeout'
CONF_FEED_INTERVAL = 'feed_interval'
CONF_TEST_MODE = 'test_mode'
CONF_CRASH_DELAY = 'crash_delay'

# Define the YAML configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(WDTManager),

    # timeout: WDT timeout period (default: 10 seconds)
    # If WDT is not fed within this period, system will reset
    cv.Optional(CONF_TIMEOUT, default='10s'): cv.positive_time_period_milliseconds,

    # feed_interval: How often to feed the WDT (default: 500ms)
    # Should be significantly less than timeout to ensure reliable feeding
    cv.Optional(CONF_FEED_INTERVAL, default='500ms'): cv.positive_time_period_milliseconds,

    # test_mode: Enable simulated crash for testing (default: false)
    # When true, stops feeding WDT after crash_delay to trigger reset
    cv.Optional(CONF_TEST_MODE, default=False): cv.boolean,

    # crash_delay: Time before simulated crash when test_mode=true (default: 20s)
    # After this delay, WDT feeding stops to test automatic reset
    cv.Optional(CONF_CRASH_DELAY, default='20s'): cv.positive_time_period_milliseconds,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    """
    Code generation function that sets up the WDTManager component.
    """
    # Create the wdt_manager instance
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set configuration parameters
    cg.add(var.set_timeout(config[CONF_TIMEOUT]))
    cg.add(var.set_feed_interval(config[CONF_FEED_INTERVAL]))
    cg.add(var.set_test_mode(config[CONF_TEST_MODE]))
    cg.add(var.set_crash_delay(config[CONF_CRASH_DELAY]))
