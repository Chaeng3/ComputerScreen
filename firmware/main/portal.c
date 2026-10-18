#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "portal.h"
#include "form.h"
#include "settings.h"
#include "network.h"
#include "weather.h"
#include "ota_update.h"
#include "lighting.h"
#include "city_ids.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern const char html_start[] asm("_binary_portal_html_start");
extern const char html_end[] asm("_binary_portal_html_end");
extern const char cities_start[] asm("_binary_cities_json_start");
extern const char cities_end[] asm("_binary_cities_json_end");
extern const char js_start[] asm("_binary_portal_js_start");
extern const char js_end[] asm("_binary_portal_js_end");
extern const char ota_js_start[] asm("_binary_ota_js_start");
extern const char ota_js_end[] asm("_binary_ota_js_end");
static bool reboot_pending;
static void no_cache(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
}

static esp_err_t page(httpd_req_t *req)
{
    no_cache(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, html_start, html_end - html_start - 1);
}

static esp_err_t state(httpd_req_t *req)
{
    network_status_t net;
    network_status(&net);
    const app_settings_t *s = settings_get();
    const lighting_settings_t *lighting = settings_get_lighting();
    char ssid[100], city[292], ap[100];
    if (!form_encode(s->ssid, ssid, sizeof(ssid)) ||
        !form_encode(s->city, city, sizeof(city)) ||
        !form_encode(net.ap_name, ap, sizeof(ap))) return ESP_FAIL;
    weather_status_t weather;
    weather_status(&weather);
    char weather_info[256], daily_info[256];
    if (!form_encode(weather_message(weather.error), weather_info, sizeof(weather_info)) ||
        !form_encode(weather_message(weather.daily_error), daily_info, sizeof(daily_info))) return ESP_FAIL;
    char body[1536];
    snprintf(body, sizeof(body),
        "ssid=%s&city=%s&brightness=%lu&day=%02lu:%02lu&dusk=%02lu:%02lu&night=%02lu:%02lu"
        "&has_password=%d&has_key=%d&online=%d&synced=%d&ap=%s&ip=%s&epoch=%lld&weather=%s&forecast=%s"
        "&auto_dim=%lu&night_brightness=%lu&brightness_now=%u",
        ssid, city, (unsigned long)s->brightness,
        (unsigned long)s->day_start / 60, (unsigned long)s->day_start % 60,
        (unsigned long)s->dusk_start / 60, (unsigned long)s->dusk_start % 60,
        (unsigned long)s->night_start / 60, (unsigned long)s->night_start % 60,
        s->password[0] != 0, s->api_key[0] != 0, net.online, net.time_synced,
        ap, net.ip, (long long)time(NULL), weather_info, daily_info,
        (unsigned long)lighting->auto_dim, (unsigned long)lighting->night_brightness, lighting_current());
    no_cache(req);
    httpd_resp_set_type(req, "application/x-www-form-urlencoded; charset=utf-8");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t cities(httpd_req_t *req)
{
    no_cache(req);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_send(req, cities_start, cities_end - cities_start - 1);
}

static esp_err_t javascript(httpd_req_t *req)
{
    no_cache(req);
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    return httpd_resp_send(req, js_start, js_end - js_start - 1);
}

static esp_err_t ota_javascript(httpd_req_t *req)
{
    no_cache(req);
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    return httpd_resp_send(req, ota_js_start, ota_js_end - ota_js_start - 1);
}

static esp_err_t wifi_scan(httpd_req_t *req)
{
    no_cache(req);
    char query[32];
    bool start = httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK && !strcmp(query, "start=1");
    esp_err_t err = start ? network_scan_start() : ESP_OK;
    network_scan_result_t *scan = malloc(sizeof(*scan));
    if (!scan) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "设备繁忙，请稍后再试");
    network_scan_result(scan);
    cJSON *root = cJSON_CreateObject();
    cJSON *list = cJSON_CreateArray();
    if (!root || !list) { free(scan); cJSON_Delete(root); cJSON_Delete(list); return ESP_FAIL; }
    cJSON_AddStringToObject(root, "status", err != ESP_OK ? "busy" :
        (scan->scanning ? "scanning" : (scan->ready ? "ready" : "idle")));
    cJSON_AddItemToObject(root, "networks", list);
    for (unsigned i = 0; i < scan->count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (!item) continue;
        cJSON_AddStringToObject(item, "ssid", scan->items[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", scan->items[i].rssi);
        cJSON_AddBoolToObject(item, "secure", scan->items[i].secure);
        cJSON_AddItemToArray(list, item);
    }
    char *body = cJSON_PrintUnformatted(root);
    free(scan);
    cJSON_Delete(root);
    if (!body) return ESP_FAIL;
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    esp_err_t result = httpd_resp_sendstr(req, body);
    cJSON_free(body);
    return result;
}

static bool city_allowed(const char *city)
{
    // Keep existing installations compatible with saved city names/pinyin.
    if (!strcmp(city, settings_get()->city)) return true;
    for (size_t i = 0; i < sizeof(mainland_city_ids) / sizeof(mainland_city_ids[0]); ++i) {
        if (!strcmp(city, mainland_city_ids[i])) return true;
    }
    return false;
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(2500));
    esp_restart();
}

static esp_err_t save(httpd_req_t *req)
{
    no_cache(req);
    if (reboot_pending) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "设备正在重启，请稍后再试");
    char type[80];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
        strncmp(type, "application/x-www-form-urlencoded", 33) != 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "提交格式不正确，请刷新设置页");
    }
    // Browser custom header prevents cross-site form submissions; CORS is not enabled.
    char marker[8];
    if (httpd_req_get_hdr_value_str(req, "X-Case-Setup", marker, sizeof(marker)) != ESP_OK ||
        strcmp(marker, "1")) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "请通过副屏设置页面提交");
    if (req->content_len <= 0 || req->content_len >= 2048) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "提交内容长度不正确");
    }
    char *body = malloc(req->content_len + 1);
    if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "设备繁忙，请稍后再试");
    int received = 0;
    while (received < req->content_len) {
        int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count <= 0) {
            free(body);
            return httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "读取超时，请重试");
        }
        received += count;
    }
    body[received] = 0;
    app_settings_t next = *settings_get();
    lighting_settings_t lighting = *settings_get_lighting();
    char password[65], key[97], value[16];
    bool ok = !memchr(body, 0, received) &&
        form_get(body, "ssid", next.ssid, sizeof(next.ssid)) && next.ssid[0] &&
        form_get(body, "password", password, sizeof(password)) &&
        form_get(body, "city", next.city, sizeof(next.city)) && next.city[0] && city_allowed(next.city) &&
        form_get(body, "api_key", key, sizeof(key)) &&
        form_get(body, "brightness", value, sizeof(value)) && value[0];
    if (ok) {
        char *end;
        long brightness = strtol(value, &end, 10);
        ok = !*end && brightness >= 1 && brightness <= 100;
        next.brightness = brightness;
        // Missing fields preserve values for older cached setup pages.
        char automatic[4];
        bool parsed = form_get(body, "auto_dim", automatic, sizeof(automatic));
        ok = ok && parsed;
        if (parsed && automatic[0]) {
            ok = ok && (!strcmp(automatic, "0") || !strcmp(automatic, "1"));
            lighting.auto_dim = !strcmp(automatic, "1");
        }
        parsed = form_get(body, "night_brightness", value, sizeof(value));
        ok = ok && parsed;
        if (parsed && value[0]) {
            long night = strtol(value, &end, 10);
            ok = ok && !*end && night >= 1 && night <= 100;
            lighting.night_brightness = night;
        }
        if (password[0]) strlcpy(next.password, password, sizeof(next.password));
        if (key[0]) strlcpy(next.api_key, key, sizeof(next.api_key));
        if (strcmp(next.ssid, settings_get()->ssid) && !password[0]) next.password[0] = 0;
        char open[4] = {0};
        ok = ok && form_get(body, "open_wifi", open, sizeof(open));
        if (!strcmp(open, "1")) next.password[0] = 0;
        ok = ok && (next.password[0] || !strcmp(open, "1") ||
            (!strcmp(next.ssid, settings_get()->ssid) && !settings_get()->password[0]));
        ok = ok && (!next.password[0] || (strlen(next.password) >= 8 && strlen(next.password) <= 63));
        ok = ok && form_get(body, "day", value, sizeof(value)) && form_minute(value, &next.day_start);
        ok = ok && form_get(body, "dusk", value, sizeof(value)) && form_minute(value, &next.dusk_start);
        ok = ok && form_get(body, "night", value, sizeof(value)) && form_minute(value, &next.night_start);
    }
    free(body);
    if (!ok || next.day_start >= next.dusk_start || next.dusk_start >= next.night_start) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "请检查无线网络、城市、亮度和主题时间");
    }
    esp_err_t err = settings_save(&next, &lighting);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "保存失败，请稍后重试");
    if (xTaskCreate(restart_task, "save_restart", 2048, NULL, 3, NULL) != pdPASS) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "已保存，请手动重启设备");
    }
    reboot_pending = true;
    ESP_LOGI("portal", "Settings saved; restarting (credentials omitted)");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "已保存，设备即将重启。请等待联网；连接失败时，设置热点会在约 30 秒后重新出现。");
}

void portal_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 10;
    config.lru_purge_enable = true;
    httpd_handle_t server;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    const httpd_uri_t routes[] = {
        {.uri="/", .method=HTTP_GET, .handler=page},
        {.uri="/api/state", .method=HTTP_GET, .handler=state},
        {.uri="/api/cities", .method=HTTP_GET, .handler=cities},
        {.uri="/api/wifi", .method=HTTP_GET, .handler=wifi_scan},
        {.uri="/portal.js", .method=HTTP_GET, .handler=javascript},
        {.uri="/ota.js", .method=HTTP_GET, .handler=ota_javascript},
        {.uri="/api/save", .method=HTTP_POST, .handler=save},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
    }
    ota_update_register(server, &reboot_pending);
    ESP_LOGI("portal", "Setup web server started");
}
