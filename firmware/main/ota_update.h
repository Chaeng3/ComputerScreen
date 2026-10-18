#pragma once
#include "esp_http_server.h"
void ota_update_register(httpd_handle_t server, bool *reboot_pending);
void ota_update_health_tick(void);
