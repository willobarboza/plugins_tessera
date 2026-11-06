"""Proxmox: the ESPHome side. The problem sensor, its names and the popup's switch are entities of plugin.yaml; this
component reads them and registers the tiles, the card, the top bar item and the screensaver (proxmox.cpp).
register_plugin() reads the id, version, tile memory and the screen's texts from the manifest and translations/ beside
this folder (docs/FIRMWARE_API.md)."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, smart_display, switch, text_sensor
from esphome.const import CONF_ID

# The core component every Tessera screen has; the plugin API lives in it.
DEPENDENCIES = ["smart_display"]

CONF_PROBLEM = "problem"
CONF_PROBLEMS = "problems"
CONF_POPUP = "popup"

proxmox_ns = cg.esphome_ns.namespace("proxmox")
Proxmox = proxmox_ns.class_("Proxmox", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Proxmox),
        cv.Required(CONF_PROBLEM): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_PROBLEMS): cv.use_id(text_sensor.TextSensor),
        cv.Required(CONF_POPUP): cv.use_id(switch.Switch),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)
    cg.add(var.set_problem(await cg.get_variable(config[CONF_PROBLEM])))
    cg.add(var.set_problems(await cg.get_variable(config[CONF_PROBLEMS])))
    cg.add(var.set_popup(await cg.get_variable(config[CONF_POPUP])))
