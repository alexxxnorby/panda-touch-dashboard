import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, time, wifi, font
from esphome.components.online_image.image import OnlineImage
from esphome.components.image import Image_
from esphome.const import CONF_ID

DEPENDENCIES = ['api', 'lvgl', 'time', 'light', 'image', 'wifi']
ns = cg.esphome_ns.namespace('workshop_panel')
WorkshopPanel = ns.class_('WorkshopPanel', cg.PollingComponent)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(WorkshopPanel),
    **{cv.Required(f'german_font_{size}'): cv.use_id(font.Font) for size in (16,20,28,48)},
    cv.Required('clock_id'): cv.use_id(time.RealTimeClock),
    cv.Required('backlight_id'): cv.use_id(light.LightState),
    cv.Required('snapshot_id'): cv.use_id(OnlineImage),
    cv.Required('wallpaper_id'): cv.use_id(OnlineImage),
    cv.Required('background_id'): cv.use_id(Image_),
}).extend(cv.polling_component_schema('1s'))

async def to_code(config):
    cg.add_define('USE_LVGL_FONT')
    wifi.request_wifi_scan_results_listener()
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    for index, size in enumerate((16,20,28,48)):
        cg.add(var.set_german_font(index, await cg.get_variable(config[f'german_font_{size}'])))
    cg.add(var.set_clock(await cg.get_variable(config['clock_id'])))
    cg.add(var.set_backlight(await cg.get_variable(config['backlight_id'])))
    cg.add(var.set_snapshot(await cg.get_variable(config['snapshot_id'])))
    cg.add(var.set_wallpaper(await cg.get_variable(config['wallpaper_id'])))
    cg.add(var.set_background(await cg.get_variable(config['background_id'])))
