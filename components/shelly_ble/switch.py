"""
Shelly BLE Switch Platform for ESPHome
Provides Bluetooth Low Energy control for Shelly Gen2+ devices (Plus 1PM, Plus 4PM, etc.)
Uses ESPHome's ble_client component for connectivity
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch, ble_client
from esphome.const import CONF_ID

DEPENDENCIES = ['ble_client']
CODEBASE_COMPATIBILITY = ['esp32']

# Namespace for the component
shelly_ble_ns = cg.esphome_ns.namespace('shelly_ble')
ShellyBLESwitch = shelly_ble_ns.class_('ShellyBLESwitch', switch.Switch, cg.Component, ble_client.BLEClientNode)

# Configuration keys
CONF_SOCKET_ID = 'socket_id'

# Switch configuration schema
CONFIG_SCHEMA = switch.switch_schema(ShellyBLESwitch).extend({
    cv.Required(CONF_SOCKET_ID): cv.int_range(min=0, max=3),  # Shelly Plus 4PM has sockets 0-3
}).extend(ble_client.BLE_CLIENT_SCHEMA)


async def to_code(config):
    """Generate code for the Shelly BLE switch"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await switch.register_switch(var, config)
    await ble_client.register_ble_node(var, config)

    # Set socket ID
    cg.add(var.set_socket_id(config[CONF_SOCKET_ID]))
