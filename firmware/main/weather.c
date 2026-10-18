#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "weather.h"
#include "weather_decode.h"
#include "settings.h"
#include "network.h"
#include "form.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"

#define BODY_CAPACITY 12288
#define CACHE_VERSION 2
static weather_status_t current;
static SemaphoreHandle_t lock;

void weather_status(weather_status_t *status)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    *status = current;
    xSemaphoreGive(lock);
}

static void publish(const weather_status_t *status)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    current = *status;
    xSemaphoreGive(lock);
}

const char *weather_message(weather_error_t error)
{
    switch (error) {
    case WEATHER_NO_KEY: return "请在设置页填写天气密钥";
    case WEATHER_NO_CITY: return "请在设置页选择城市";
    case WEATHER_NETWORK: return "天气暂未更新";
    case WEATHER_TIME: return "正在校准时间";
    case WEATHER_AUTH: return "天气密钥无效或已失效";
    case WEATHER_PERMISSION: return "天气接口权限不足";
    case WEATHER_CITY: return "请检查中国大陆城市设置";
    case WEATHER_FAILED: return "天气暂不可用，稍后重试";
    case WEATHER_OK: return "天气已更新";
    default: return "正在获取天气";
    }
}

typedef struct { char *data; size_t used; bool overflow; } response_t;

static esp_err_t collect(esp_http_client_event_t *event)
{
    response_t *body = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        if ((size_t)event->data_len >= BODY_CAPACITY - body->used) {
            body->overflow = true;
            return ESP_FAIL;
        }
        memcpy(body->data + body->used, event->data, event->data_len);
        body->used += event->data_len;
        body->data[body->used] = 0;
    }
    return ESP_OK;
}

static weather_error_t fetch(bool daily, bool extended, weather_status_t *snapshot)
{
    const app_settings_t *settings = settings_get();
    char city[292], key[292], url[1024];
    if (!form_encode(settings->city, city, sizeof(city)) ||
        !form_encode(settings->api_key, key, sizeof(key))) return WEATHER_FAILED;
    snprintf(url, sizeof(url),
        "https://api.seniverse.com/v3/weather/%s.json?key=%s&location=%s&language=zh-Hans&unit=c%s",
        daily ? "daily" : "now", key, city, daily ?
            (extended ? "&start=0&days=4" : "&start=0&days=3") : "");
    response_t body = {.data = calloc(1, BODY_CAPACITY)};
    if (!body.data) return WEATHER_FAILED;
    const esp_http_client_config_t config = {
        .url = url, .timeout_ms = 10000, .event_handler = collect, .user_data = &body,
        .crt_bundle_attach = esp_crt_bundle_attach, .buffer_size = 2048,
        .buffer_size_tx = 1536, .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    weather_error_t result = WEATHER_FAILED;
    if (client) {
        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        if (err == ESP_OK && !body.overflow && body.used) {
            result = weather_decode(body.data, daily, snapshot);
            if (status != 200 && result == WEATHER_OK) result = WEATHER_FAILED;
        }
        // Never log URLs or response bodies because they may contain credentials.
        ESP_LOGI("weather", "%s request: HTTP=%d result=%d", daily ? "daily" : "current", status, result);
        if (daily && result == WEATHER_OK) ESP_LOGI("weather", "Forecast days loaded: %u", snapshot->forecast_count);
        esp_http_client_cleanup(client);
    }
    memset(key, 0, sizeof(key));
    memset(url, 0, sizeof(url));
    free(body.data);
    return result;
}

static void cache_save(const weather_status_t *snapshot)
{
    nvs_handle_t handle;
    if (nvs_open("weather", NVS_READWRITE, &handle) != ESP_OK) return;
    esp_err_t err = nvs_set_blob(handle, "cache", snapshot, sizeof(*snapshot));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) ESP_LOGW("weather", "Could not save weather cache");
}

static void weather_task(void *arg)
{
    (void)arg;
    int64_t next_current = 0, next_daily = 0;
    unsigned retry = 60;
    bool extended_forecast = true;
    bool was_online = false;
    char requested_day[11] = "";
    while (true) {
        network_status_t net;
        network_status(&net);
        if (net.online && !was_online) {
            next_current = 0;
            next_daily = 0;
            retry = 60;
        }
        was_online = net.online;
        weather_status_t snapshot;
        weather_status(&snapshot);
        const app_settings_t *settings = settings_get();
        if (!settings->api_key[0]) snapshot.error = WEATHER_NO_KEY;
        else if (!settings->city[0]) snapshot.error = WEATHER_NO_CITY;
        else if (!net.online) snapshot.error = WEATHER_NETWORK;
        else if (!net.time_synced) snapshot.error = WEATHER_TIME;
        else {
            int64_t now = esp_timer_get_time() / 1000000;
            time_t wall_time = time(NULL);
            struct tm local;
            localtime_r(&wall_time, &local);
            char today[11];
            strftime(today, sizeof(today), "%Y-%m-%d", &local);
            if (strcmp(today, requested_day)) next_daily = 0;
            bool changed = false;
            if (now >= next_current) {
                snapshot.error = fetch(false, false, &snapshot);
                if (snapshot.error == WEATHER_OK) { retry = 60; changed = true; }
                next_current = now + (snapshot.error == WEATHER_OK ? 900 : retry);
                if (snapshot.error != WEATHER_OK && retry < 900) retry = retry * 2 > 900 ? 900 : retry * 2;
                publish(&snapshot);
                // Personal plans may limit requests to one per second.
                vTaskDelay(pdMS_TO_TICKS(1200));
            }
            if (now >= next_daily) {
                // Mark the attempted day even on failure so backoff still applies.
                strlcpy(requested_day, today, sizeof(requested_day));
                snapshot.daily_error = fetch(true, extended_forecast, &snapshot);
                if (extended_forecast && (snapshot.daily_error == WEATHER_PERMISSION || snapshot.daily_error == WEATHER_RANGE)) {
                    extended_forecast = false;
                    vTaskDelay(pdMS_TO_TICKS(1200));
                    snapshot.daily_error = fetch(true, false, &snapshot);
                }
                if (snapshot.daily_error == WEATHER_OK && snapshot.forecast_count < WEATHER_FORECAST_MAX)
                    extended_forecast = false;
                next_daily = now + (snapshot.daily_error == WEATHER_OK ? 3600 : 900);
                changed |= snapshot.daily_error == WEATHER_OK;
            }
            publish(&snapshot);
            if (changed) cache_save(&snapshot);
        }
        publish(&snapshot);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void weather_init(void)
{
    lock = xSemaphoreCreateMutex();
    assert(lock);
    current.version = CACHE_VERSION;
    current.error = settings_get()->api_key[0] ? WEATHER_WAIT : WEATHER_NO_KEY;
    strlcpy(current.configured_city, settings_get()->city, sizeof(current.configured_city));
    nvs_handle_t handle;
    if (nvs_open("weather", NVS_READONLY, &handle) == ESP_OK) {
        weather_status_t cached;
        size_t size = sizeof(cached);
        if (nvs_get_blob(handle, "cache", &cached, &size) == ESP_OK && size == sizeof(cached) &&
            cached.version == CACHE_VERSION && memchr(cached.configured_city, 0, sizeof(cached.configured_city)) &&
            memchr(cached.city, 0, sizeof(cached.city)) && memchr(cached.text, 0, sizeof(cached.text)) &&
            memchr(cached.daily_date, 0, sizeof(cached.daily_date)) &&
            cached.forecast_count <= WEATHER_FORECAST_MAX &&
            !strcmp(cached.configured_city, settings_get()->city)) {
            current = cached;
            current.error = WEATHER_WAIT;
            for (unsigned i = 0; i < current.forecast_count; ++i) {
                if (!memchr(current.forecast[i].date, 0, sizeof(current.forecast[i].date)) ||
                    !memchr(current.forecast[i].text, 0, sizeof(current.forecast[i].text))) {
                    current.forecast_count = 0;
                    current.daily_valid = false;
                    break;
                }
            }
        }
        nvs_close(handle);
    }
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_WARN);
    BaseType_t created = xTaskCreate(weather_task, "weather", 12288, NULL, 3, NULL);
    assert(created == pdPASS);
}
