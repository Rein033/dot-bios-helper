# The language, the list, the glass and the keys (script.c, store.c, ui.c, gfx.c, keys.c) are
# pure C and run in the host tests too; the Active Object, the keyboard and the web server bring
# the SDK, TinyUSB and ESP-IDF.
idf_component_register(
    SRCS "app_main.c" "app.c" "script.c" "keys.c" "builtin.c" "store.c" "ui.c" "gfx.c"
         "fonts_aa.c" "icons_aa.c" "hidkb.c" "web.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "web/index.html"
    REQUIRES kode
    PRIV_REQUIRES kdrv khal esp_hw_support esp_hal_usb esp_timer esp_http_server
)

# TinyUSB reads its settings from a tusb_config.h on its own include path: ours (main/tusb/).
idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/tusb")
