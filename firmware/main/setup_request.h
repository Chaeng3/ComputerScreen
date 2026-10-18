#pragma once
#include <stdbool.h>
#include <string.h>
#include "esp_http_server.h"

static inline bool computerscreen_setup_request(httpd_req_t *req)
{
    char marker[8];
    if (httpd_req_get_hdr_value_str(req, "X-ComputerScreen-Setup", marker, sizeof(marker)) == ESP_OK)
        return !strcmp(marker, "1");
    // Accept cached pages and tools from the pre-rename firmware.
    return httpd_req_get_hdr_value_str(req, "X-Case-Setup", marker, sizeof(marker)) == ESP_OK && !strcmp(marker, "1");
}
