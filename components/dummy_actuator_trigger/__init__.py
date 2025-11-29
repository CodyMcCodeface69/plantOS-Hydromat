"""
DummyActuatorTrigger ESPHome Component

Test component that validates ActuatorSafetyGate functionality by
running automated test sequences for debouncing and duration limits.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome import pins

# Import the safety gate namespace
actuator_safety_gate_ns = cg.esphome_ns.namespace('actuator_safety_gate')
ActuatorSafetyGate = actuator_safety_gate_ns.class_('ActuatorSafetyGate')

# Define our namespace and class
dummy_actuator_trigger_ns = cg.esphome_ns.namespace('dummy_actuator_trigger')
DummyActuatorTrigger = dummy_actuator_trigger_ns.class_('DummyActuatorTrigger', cg.Component)

# Configuration constants
CONF_SAFETY_GATE = 'safety_gate'
CONF_LED_PIN = 'led_pin'
CONF_TEST_INTERVAL = 'test_interval'

# Configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(DummyActuatorTrigger),
    cv.Required(CONF_SAFETY_GATE): cv.use_id(ActuatorSafetyGate),
    cv.Optional(CONF_LED_PIN): pins.gpio_output_pin_schema,
    cv.Optional(CONF_TEST_INTERVAL, default='10s'): cv.positive_time_period_milliseconds,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    """Generate C++ code for this component"""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Link to safety gate
    safety_gate = await cg.get_variable(config[CONF_SAFETY_GATE])
    cg.add(var.set_safety_gate(safety_gate))

    # Configure LED pin if specified
    if CONF_LED_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_LED_PIN])
        cg.add(var.set_led_pin(pin))

    # Set test interval
    cg.add(var.set_test_interval(config[CONF_TEST_INTERVAL]))
