"""
AlertService ESPHome Component

Centralized alert dispatching with rate limiting and pluggable backends.
Default backend: LogAlertBackend (ESP_LOG output).
Future backends: TelegramAlertBackend, HAAlertBackend.

Usage in YAML:
  alert_service:
    id: alerts
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

alert_service_ns = cg.global_ns.namespace('alert_service')
AlertService = alert_service_ns.class_('AlertService', cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(AlertService),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
