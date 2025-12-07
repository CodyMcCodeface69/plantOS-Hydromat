"""
i2c_scanner: ESPHome Component for I²C Bus Scanning

This component scans the I²C bus for connected devices and validates the
presence of critical devices required by the PlantOS system.

FEATURES:
- Scans all valid 7-bit I²C addresses (0x01 to 0x77)
- Identifies connected devices by address
- Validates critical device presence (ADS1115, MCP23017, BME280, etc.)
- Provides detailed logging with warnings for missing critical devices
- Supports one-time scan on boot or periodic scanning

USAGE:
i2c_scanner:
  id: my_scanner
  i2c_id: i2c_bus
  scan_on_boot: true       # Scan once at startup
  scan_interval: 0s        # 0 = scan only on boot, >0 = periodic scanning

  # Optional: Define critical devices for your system
  critical_devices:
    - address: 0x48
      name: "ADS1115 ADC"
    - address: 0x20
      name: "MCP23017 GPIO Expander"
    - address: 0x76
      name: "BME280 Sensor"
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.const import CONF_ID

# Create a C++ namespace for the i2c_scanner component
i2c_scanner_ns = cg.esphome_ns.namespace('i2c_scanner')

# Declare the C++ I2CScanner class inheriting from Component and I2CDevice
I2CScanner = i2c_scanner_ns.class_('I2CScanner', cg.Component, i2c.I2CDevice)

# Configuration keys
CONF_SCAN_ON_BOOT = 'scan_on_boot'
CONF_SCAN_INTERVAL = 'scan_interval'
CONF_CRITICAL_DEVICES = 'critical_devices'
CONF_ADDRESS = 'address'
CONF_NAME = 'name'
CONF_STATUS_LOGGER = 'status_logger'
CONF_VERBOSE = 'verbose'

# Critical device schema
CRITICAL_DEVICE_SCHEMA = cv.Schema({
    cv.Required(CONF_ADDRESS): cv.i2c_address,
    cv.Required(CONF_NAME): cv.string,
})

# Define the YAML configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(I2CScanner),

    # scan_on_boot: Whether to run scan once at boot (default: true)
    cv.Optional(CONF_SCAN_ON_BOOT, default=True): cv.boolean,

    # scan_interval: How often to scan (0 = only on boot, >0 = periodic)
    cv.Optional(CONF_SCAN_INTERVAL, default='0s'): cv.positive_time_period_milliseconds,

    # critical_devices: List of devices that must be present
    cv.Optional(CONF_CRITICAL_DEVICES, default=[]): cv.All(
        cv.ensure_list(CRITICAL_DEVICE_SCHEMA),
    ),

    # status_logger: Optional reference to controller for status reporting
    cv.Optional(CONF_STATUS_LOGGER): cv.use_id(cg.Component),

    # verbose: Enable detailed scan logging (default: true)
    cv.Optional(CONF_VERBOSE, default=True): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA).extend(i2c.i2c_device_schema(0x00))

async def to_code(config):
    """
    Code generation function that sets up the I2CScanner component.

    This generates C++ code to:
    1. Instantiate the I2CScanner object
    2. Register it as a component (enables setup()/loop() lifecycle)
    3. Configure scan settings (on_boot, interval)
    4. Register critical devices for validation
    """
    # Create the i2c_scanner instance
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    # Configure scan settings
    cg.add(var.set_scan_on_boot(config[CONF_SCAN_ON_BOOT]))
    cg.add(var.set_scan_interval(config[CONF_SCAN_INTERVAL]))
    cg.add(var.set_verbose(config[CONF_VERBOSE]))

    # Register critical devices
    for device in config[CONF_CRITICAL_DEVICES]:
        cg.add(var.add_critical_device(device[CONF_ADDRESS], device[CONF_NAME]))

    # Link status logger if provided
    if CONF_STATUS_LOGGER in config:
        logger = await cg.get_variable(config[CONF_STATUS_LOGGER])
        cg.add(var.set_status_logger(logger.getStatusLogger()))
