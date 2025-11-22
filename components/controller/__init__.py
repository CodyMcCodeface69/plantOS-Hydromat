"""
controller: ESPHome Finite State Machine Controller Component

This module defines the YAML schema for a generic FSM-based controller that
links sensor inputs to visual LED feedback outputs.

ARCHITECTURE OVERVIEW:
This controller implements the Model-View-Controller (MVC) pattern within an
embedded context:
- Model: Sensor data (sensor_source)
- View: LED visual feedback (light_target)
- Controller: FSM logic (this component)

WHY THIS DESIGN:
1. LOOSE COUPLING: The controller doesn't know about specific sensor or LED
   types. It works with any ESPHome sensor and any light component via their
   abstract interfaces (sensor::Sensor and light::LightState).

2. TESTABILITY: You can swap the dummy sensor for a real sensor, or change
   the LED type, without modifying controller code. Different YAML configs
   can test different hardware combinations.

3. REUSABILITY: The same controller logic can be used in different projects
   with different sensors/LEDs just by changing the YAML configuration.

4. TYPE SAFETY: cv.use_id() ensures that the referenced IDs actually exist
   and are of the correct type at compile time, catching configuration errors
   early.

COMPONENT LINKING PATTERN:
The controller receives pointers to sensor and light objects via setter methods.
This is ESPHome's standard dependency injection pattern:

1. YAML defines component IDs and relationships
2. Python codegen retrieves component references via cg.get_variable()
3. Python codegen calls setter methods to inject dependencies
4. C++ code can then call methods on the injected components

This is safer than global variables and more flexible than hard-coded references.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, light, text_sensor
from esphome.const import CONF_ID

# Create a C++ namespace for this controller
controller_ns = cg.esphome_ns.namespace('controller')

# Declare the C++ Controller class
# WHY ONLY Component INHERITANCE (not PollingComponent):
# The FSM needs to run on every loop() iteration to maintain responsive
# animations (breathing effect, fast blinking). PollingComponent would add
# unnecessary timing overhead. The controller manages its own timing via
# state_start_time_ and millis() for precise control over state transitions.
Controller = controller_ns.class_('Controller', cg.Component)

# Configuration keys for YAML
CONF_SENSOR_SOURCE = 'sensor_source'
CONF_LIGHT_TARGET = 'light_target'
CONF_STATE_TEXT = 'state_text'

# Define the YAML configuration schema
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Controller),

    # sensor_source: Required reference to any sensor component
    # cv.use_id() validates that:
    # - The ID exists in the configuration
    # - The ID refers to a component of type sensor.Sensor
    # - The reference can be resolved at compile time
    cv.Required(CONF_SENSOR_SOURCE): cv.use_id(sensor.Sensor),

    # light_target: Required reference to any light component
    # Using light.LightState (not a specific LED type like NeoPixel) allows
    # this controller to work with any ESPHome light: addressable LEDs,
    # RGB lights, RGBW lights, single-channel lights, etc.
    cv.Required(CONF_LIGHT_TARGET): cv.use_id(light.LightState),

    # state_text: Optional reference to a text sensor to publish FSM state
    # Allows exposing the current state (INIT, CALIBRATION, READY, ERROR)
    # in the web UI and for monitoring/debugging purposes
    cv.Optional(CONF_STATE_TEXT): cv.use_id(text_sensor.TextSensor),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    """
    Code generation function for dependency injection.

    This function runs during ESPHome compilation and generates C++ code to:
    1. Instantiate the Controller object
    2. Register it as a component (enables setup()/loop() lifecycle)
    3. Inject sensor and light dependencies via setter methods

    WHY ASYNC:
    cg.get_variable() may need to wait for other components to be generated
    first (dependency resolution). Python's async/await handles this ordering
    automatically.
    """
    # Create the controller instance
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Retrieve the sensor component by ID and inject it into the controller
    # This generates C++ code like: controller->set_sensor_source(sensor_dummy_id);
    sens = await cg.get_variable(config[CONF_SENSOR_SOURCE])
    cg.add(var.set_sensor_source(sens))

    # Retrieve the light component by ID and inject it into the controller
    # This generates C++ code like: controller->set_light_target(system_led);
    lit = await cg.get_variable(config[CONF_LIGHT_TARGET])
    cg.add(var.set_light_target(lit))

    # Optionally inject a text sensor for state publishing
    # This allows the controller to expose its current FSM state in the web UI
    if CONF_STATE_TEXT in config:
        state_text = await cg.get_variable(config[CONF_STATE_TEXT])
        cg.add(var.set_state_text(state_text))
