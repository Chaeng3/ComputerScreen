#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "display.h"
#include "settings.h"
#include "network.h"
#include "portal.h"
#include "weather.h"
#include "clock_ui.h"
#include "ota_update.h"
#include "lighting.h"

void app_main(void)
{
    settings_init();
    setenv("TZ", "CST-8", 1);
    tzset();
    display_init();
    lighting_init();
    display_clear(0x0000);
    display_utf8(109, 90, "正在启动", 0xffff);
    display_present();
    network_init();
    weather_init();
    portal_init();
    ESP_LOGI("clock", "Chinese clock and Seniverse weather ready");
    while (true) {
        lighting_tick();
        clock_ui_render();
        ota_update_health_tick();
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}
