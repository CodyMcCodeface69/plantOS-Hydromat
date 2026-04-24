"""ESPHome component for Atlas Scientific EZO pH sensor via I2C."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c, sensor
from esphome.const import CONF_ID

DEPENDENCIES = ["i2c"]
CODEOWNERS = ["@plantOS"]

ezo_ph_ns = cg.esphome_ns.namespace("ezo_ph")
EZOPHComponent = ezo_ph_ns.class_("EZOPHComponent", cg.PollingComponent, i2c.I2CDevice)

CONF_EZO_PH_ID = "ezo_ph_id"
CONF_PH = "ph"
CONF_TEMPERATURE_COMPENSATION = "temperature_compensation"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(EZOPHComponent),
            cv.Optional(CONF_PH): sensor.sensor_schema(
                unit_of_measurement="pH",
                accuracy_decimals=2,
                icon="mdi:ph",
            ),
            cv.Optional(CONF_TEMPERATURE_COMPENSATION): cv.use_id(sensor.Sensor),
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(i2c.i2c_device_schema(0x61))
)


async def to_code(config):
    """Generate code for the EZO pH component."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    # Register pH sensor if configured
    if CONF_PH in config:
        sens = await sensor.new_sensor(config[CONF_PH])
        cg.add(var.set_ph_sensor(sens))

    # Register temperature compensation sensor if configured
    if CONF_TEMPERATURE_COMPENSATION in config:
        temp_sens = await cg.get_variable(config[CONF_TEMPERATURE_COMPENSATION])
        cg.add(var.set_temperature_compensation(temp_sens))
