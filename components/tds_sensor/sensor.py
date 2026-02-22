"""
tds_sensor: ESPHome TDS/EC Sensor Component

Reads raw ADC voltage from a KS0429 TDS Meter V1.0, applies median filtering
and temperature compensation, converts to EC (uS/cm) using the KS0429 polynomial,
and publishes the result as an ESPHome sensor.

Hardware: KS0429 TDS Meter V1.0 (3.3-5.5V input, 0-2.3V output, 0-1000ppm range)
Conversion: EC (uS/cm) = 133.42*V^3 - 255.86*V^2 + 857.39*V
Temp compensation: compensatedV = voltage / (1.0 + 0.02 * (temperature - 25.0))

DESIGN DECISIONS:
- PollingComponent: Publishes EC at fixed intervals (default 5s)
- Event-driven ADC buffering: Subscribes to source sensor callbacks for voltage readings
- Median filter: More robust than mean for rejecting outliers from ADC noise
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID

# Configuration keys
CONF_SENSOR_SOURCE = "sensor_source"
CONF_TEMPERATURE_SENSOR = "temperature_sensor"
CONF_SAMPLE_COUNT = "sample_count"
CONF_DEFAULT_TEMPERATURE = "default_temperature"

# Create C++ namespace
tds_ns = cg.esphome_ns.namespace('tds_sensor')

# Declare the C++ class TDSSensor that inherits from:
# - sensor.Sensor: Provides sensor state, publishing, callbacks
# - cg.PollingComponent: Provides update() method called at fixed intervals
TDSSensor = tds_ns.class_('TDSSensor', sensor.Sensor, cg.PollingComponent)

# Define configuration schema
CONFIG_SCHEMA = sensor.sensor_schema(
    TDSSensor,
    unit_of_measurement="uS/cm",
    accuracy_decimals=1,
).extend({
    # sensor_source: Reference to ADC sensor providing raw voltage
    cv.Required(CONF_SENSOR_SOURCE): cv.use_id(sensor.Sensor),

    # temperature_sensor: Optional reference to DS18B20 for temp compensation
    cv.Optional(CONF_TEMPERATURE_SENSOR): cv.use_id(sensor.Sensor),

    # sample_count: Number of voltage readings to buffer before calculating median
    cv.Optional(CONF_SAMPLE_COUNT, default=30): cv.int_range(min=3, max=100),

    # default_temperature: Fallback temperature if no sensor is available
    cv.Optional(CONF_DEFAULT_TEMPERATURE, default=25.0): cv.float_range(min=0.0, max=50.0),
}).extend(cv.polling_component_schema('5s'))


async def to_code(config):
    """
    Code generation function called during ESPHome compilation.

    Creates the TDSSensor instance, registers it as component and sensor,
    links the source ADC sensor and optional temperature sensor.
    """
    var = cg.new_Pvariable(config[CONF_ID])

    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)

    # Link source ADC sensor
    sensor_source = await cg.get_variable(config[CONF_SENSOR_SOURCE])
    cg.add(var.set_sensor_source(sensor_source))

    # Link optional temperature sensor
    if CONF_TEMPERATURE_SENSOR in config:
        temp_sensor = await cg.get_variable(config[CONF_TEMPERATURE_SENSOR])
        cg.add(var.set_temperature_sensor(temp_sensor))

    # Configure sample count and default temperature
    cg.add(var.set_sample_count(config[CONF_SAMPLE_COUNT]))
    cg.add(var.set_default_temperature(config[CONF_DEFAULT_TEMPERATURE]))
