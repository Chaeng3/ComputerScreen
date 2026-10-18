#include <string.h>
#include <stdbool.h>
#include "settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

// Legacy NVS namespace is a storage compatibility key, not the product name.
#define SETTINGS_VERSION 1
_Static_assert(sizeof(app_settings_t) == 312, "Keep the v1 NVS settings layout compatible with baseline OTA firmware");
static app_settings_t current;
static lighting_settings_t current_lighting;

static bool valid_lighting(const lighting_settings_t *s)
{
    return s->version == 1 && s->auto_dim <= 1 && s->night_brightness >= 1 && s->night_brightness <= 100;
}

static bool valid(const app_settings_t *s)
{
    return s->version == SETTINGS_VERSION &&
        memchr(s->ssid, 0, sizeof(s->ssid)) &&
        memchr(s->password, 0, sizeof(s->password)) &&
        memchr(s->city, 0, sizeof(s->city)) &&
        memchr(s->api_key, 0, sizeof(s->api_key)) &&
        s->brightness >= 1 && s->brightness <= 100 &&
        s->day_start < s->dusk_start && s->dusk_start < s->night_start &&
        s->night_start < 1440;
}

void settings_init(void)
{
    // Preserve existing NVS on an initialization error instead of erasing it.
    ESP_ERROR_CHECK(nvs_flash_init());
    current = (app_settings_t) {
        .version = SETTINGS_VERSION, .brightness = 25,
        .day_start = 360, .dusk_start = 1020, .night_start = 1140,
    };
    nvs_handle_t handle;
    if (nvs_open("case_display", NVS_READONLY, &handle) == ESP_OK) {
        app_settings_t stored;
        size_t size = sizeof(stored);
        if (nvs_get_blob(handle, "settings", &stored, &size) == ESP_OK &&
            size == sizeof(stored) && valid(&stored)) {
            current = stored;
        }
        nvs_close(handle);
    }
    current_lighting = (lighting_settings_t) {
        .version = 1, .auto_dim = 1,
        .night_brightness = current.brightness < 8 ? current.brightness : 8,
    };
    if (nvs_open("case_display", NVS_READONLY, &handle) == ESP_OK) {
        lighting_settings_t stored;
        size_t size = sizeof(stored);
        if (nvs_get_blob(handle, "lighting", &stored, &size) == ESP_OK &&
            size == sizeof(stored) && valid_lighting(&stored)) current_lighting = stored;
        nvs_close(handle);
    }
    ESP_LOGI("settings", "Configuration loaded; Wi-Fi %s", current.ssid[0] ? "configured" : "not configured");
}

const app_settings_t *settings_get(void) { return &current; }
const lighting_settings_t *settings_get_lighting(void) { return &current_lighting; }

esp_err_t settings_save(const app_settings_t *settings, const lighting_settings_t *lighting)
{
    if (!valid(settings) || !valid_lighting(lighting)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("case_display", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, "settings", settings, sizeof(*settings));
    if (err == ESP_OK) err = nvs_set_blob(handle, "lighting", lighting, sizeof(*lighting));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    // Active configuration is immutable until the scheduled reboot.
    return err;
}
