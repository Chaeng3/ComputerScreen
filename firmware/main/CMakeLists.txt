idf_component_register(SRCS "main.c" "display.c" "settings.c" "network.c" "portal.c" "form.c"
                           "weather.c" "weather_decode.c" "clock_ui.c" "weather_layout.c" "ota_update.c" "lighting.c"
    INCLUDE_DIRS "." EMBED_TXTFILES "portal.html" "portal.js" "ota.js" "cities.json"
    EMBED_FILES "font_cn16.bin"
    REQUIRES esp_lcd esp_driver_spi esp_driver_gpio esp_driver_ledc esp_psram
             nvs_flash app_update esp_wifi esp_event esp_netif esp_http_server esp_http_client esp_timer lwip mbedtls
             espressif__cjson espressif__mdns)
