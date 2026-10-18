#pragma once
#include "esp_err.h"
#include <stdint.h>

typedef struct {
    uint32_t version;
    char ssid[33];
    char password[65];
    char city[97];
    char api_key[97];
    uint32_t brightness;
    uint32_t day_start;
    uint32_t dusk_start;
    uint32_t night_start;
} app_settings_t;

// Stored under a separate NVS key so the baseline OTA firmware can still read
// the unchanged v1 network/settings blob after a rollback.
typedef struct {
    uint32_t version;
    uint32_t auto_dim;
    uint32_t night_brightness;
} lighting_settings_t;

void settings_init(void);
const app_settings_t *settings_get(void);
const lighting_settings_t *settings_get_lighting(void);
esp_err_t settings_save(const app_settings_t *settings, const lighting_settings_t *lighting);
