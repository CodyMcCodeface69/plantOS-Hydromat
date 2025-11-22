"""
sensor_dummy: ESPHome Configuration Schema and Code Generation

This Python module defines the YAML configuration schema for the dummy sensor
component and generates the necessary C++ code during ESPHome compilation.

WHY THIS COMPONENT EXISTS:
- Provides predictable, repeatable data for testing controller logic
- Allows development and debugging without physical sensors
- Demonstrates ESPHome's custom component architecture
- Serves as a template for implementing real sensor components

DESIGN DECISIONS:
- PollingComponent inheritance: Sensors that need periodic updates (vs
  event-driven sensors that respond to interrupts/callbacks)
- 1 second default polling: Fast enough to observe behavior, slow enough
  to not spam logs or waste CPU cycles
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID

# Create a C++ namespace to avoid naming conflicts with other components
# This maps to namespace esphome::sensor_dummy in the generated code
dummy_ns = cg.esphome_ns.namespace('sensor_dummy')

# Declare the C++ class SensorDummy that inherits from:
# - sensor.Sensor: Provides state storage, callbacks, filtering, etc.
# - cg.PollingComponent: Provides update() method called at fixed intervals
#
# WHY MULTIPLE INHERITANCE:
# - Sensor alone doesn't provide timing mechanism
# - PollingComponent alone doesn't provide sensor-specific features
# - This pattern is standard across ESPHome for polling sensors
SensorDummy = dummy_ns.class_('SensorDummy', sensor.Sensor, cg.PollingComponent)

# Extend the base sensor schema with polling component timing configuration
# This allows users to specify update_interval in YAML
CONFIG_SCHEMA = sensor.SENSOR_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(SensorDummy),
}).extend(cv.polling_component_schema('1s'))  # Default polling 1s

async def to_code(config):
    """
    Code generation function called during ESPHome compilation.

    This function generates C++ code that:
    1. Instantiates the SensorDummy object
    2. Registers it as a component (enables setup()/loop() lifecycle)
    3. Registers it as a sensor (enables Home Assistant integration, filtering)

    The order matters: component registration must happen before sensor
    registration because sensor registration may depend on component features.
    """
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)
