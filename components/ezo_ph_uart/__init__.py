"""ESPHome component for Atlas Scientific EZO pH sensor via UART."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, sensor
from esphome.const import CONF_ID

# This component depends on UART instead of I2C
DEPENDENCIES = ["uart"]
CODEOWNERS = ["@plantOS"]

# Define component namespace and class
ezo_ph_uart_ns = cg.esphome_ns.namespace("ezo_ph_uart")
EZOPHUARTComponent = ezo_ph_uart_ns.class_(
    "EZOPHUARTComponent", cg.PollingComponent, uart.UARTDevice
)

# Configuration keys
CONF_EZO_PH_ID = "ezo_ph_uart_id"
CONF_PH = "ph"
CONF_TEMPERATURE_COMPENSATION = "temperature_compensation"

# Configuration schema for YAML validation
CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(EZOPHUARTComponent),
            # pH sensor output (optional)
            cv.Optional(CONF_PH): sensor.sensor_schema(
                unit_of_measurement="pH",
                accuracy_decimals=2,
                icon="mdi:ph",
            ),
            # Temperature compensation sensor input (optional)
            cv.Optional(CONF_TEMPERATURE_COMPENSATION): cv.use_id(sensor.Sensor),
        }
    )
    .extend(cv.polling_component_schema("60s"))  # Default: poll every 60 seconds
    .extend(uart.UART_DEVICE_SCHEMA)  # Include UART configuration
)


async def to_code(config):
    """Generate C++ code for the EZO pH UART component."""
    # Create and register the component
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    # Register pH sensor output if configured
    if CONF_PH in config:
        sens = await sensor.new_sensor(config[CONF_PH])
        cg.add(var.set_ph_sensor(sens))

    # Register temperature compensation sensor input if configured
    if CONF_TEMPERATURE_COMPENSATION in config:
        temp_sens = await cg.get_variable(config[CONF_TEMPERATURE_COMPENSATION])
        cg.add(var.set_temperature_compensation(temp_sens))
