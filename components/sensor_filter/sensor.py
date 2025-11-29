"""
sensor_filter: ESPHome Sensor Filter with Robust Averaging

This component acts as a filtering layer between a source sensor and consumers.
It collects readings from a source sensor, applies robust averaging with outlier
rejection using the RobustAverager class, and publishes filtered values.

WHY THIS COMPONENT EXISTS:
- Rejects sensor outliers and noise (spikes, EMI interference, glitches)
- Provides statistically robust averages by rejecting extreme values
- Works as a drop-in filter for any ESPHome sensor
- Improves data quality for decision-making components (controllers, automations)

DESIGN DECISIONS:
- Component (not PollingComponent): Filter is event-driven, triggered by source sensor updates
- Sensor inheritance: Filter itself acts as a sensor, can be chained or used by other components
- Configurable window size and rejection percentage for different use cases
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID

# Configuration keys
CONF_SENSOR_SOURCE = "sensor_source"
CONF_WINDOW_SIZE = "window_size"
CONF_REJECT_PERCENTAGE = "reject_percentage"

# Create C++ namespace
filter_ns = cg.esphome_ns.namespace('sensor_filter')

# Declare the C++ class SensorFilter that inherits from:
# - sensor.Sensor: Provides sensor state, publishing, callbacks
# - cg.Component: Provides setup() and loop() lifecycle methods
#
# WHY Component (not PollingComponent):
# This filter is event-driven - it reacts to source sensor updates via callbacks
# rather than polling at fixed intervals. The source sensor handles timing.
SensorFilter = filter_ns.class_('SensorFilter', sensor.Sensor, cg.Component)

# Define configuration schema
CONFIG_SCHEMA = sensor.SENSOR_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(SensorFilter),

    # sensor_source: Reference to the sensor to filter
    cv.Required(CONF_SENSOR_SOURCE): cv.use_id(sensor.Sensor),

    # window_size: Number of readings to collect before calculating average
    # Default: 20 readings (good balance between responsiveness and filtering)
    cv.Optional(CONF_WINDOW_SIZE, default=20): cv.int_range(min=2, max=100),

    # reject_percentage: Percentage to reject from each end (0.0 to 0.5)
    # Default: 0.10 = 10% from bottom + 10% from top = 20% total rejection
    # Examples:
    #   0.10 with 20 readings = reject 2 lowest + 2 highest = 16 values averaged
    #   0.20 with 20 readings = reject 4 lowest + 4 highest = 12 values averaged
    cv.Optional(CONF_REJECT_PERCENTAGE, default=0.10): cv.float_range(min=0.0, max=0.5),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    """
    Code generation function called during ESPHome compilation.

    This function:
    1. Creates the SensorFilter instance
    2. Registers it as a component (enables lifecycle methods)
    3. Registers it as a sensor (enables publishing and Home Assistant integration)
    4. Links the source sensor (sets up callback subscription)
    5. Configures window size and rejection percentage
    """
    # Create the SensorFilter instance
    var = cg.new_Pvariable(config[CONF_ID])

    # Register as component and sensor
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)

    # Get reference to source sensor and link it
    sensor_source = await cg.get_variable(config[CONF_SENSOR_SOURCE])
    cg.add(var.set_sensor_source(sensor_source))

    # Configure window size and rejection percentage
    cg.add(var.set_window_size(config[CONF_WINDOW_SIZE]))
    cg.add(var.set_reject_percentage(config[CONF_REJECT_PERCENTAGE]))
