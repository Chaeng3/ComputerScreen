#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "network.h"
#include "settings.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mdns.h"

#define ONLINE BIT0
#define AP_ACTIVE BIT1
#define SYNCED BIT2
static EventGroupHandle_t flags;
static esp_netif_t *station;
static char ap_name[33];
static char ap_password[16];
static const char *TAG = "network";
static SemaphoreHandle_t scan_lock;
static network_scan_result_t scan_result;

static void scan_finished(void)
{
    wifi_ap_record_t *records = calloc(NETWORK_SCAN_MAX, sizeof(*records));
    uint16_t count = NETWORK_SCAN_MAX;
    esp_err_t err = records ? esp_wifi_scan_get_ap_records(&count, records) : ESP_ERR_NO_MEM;
    xSemaphoreTake(scan_lock, portMAX_DELAY);
    scan_result.scanning = false;
    scan_result.ready = err == ESP_OK;
    scan_result.count = 0;
    if (err == ESP_OK) {
        for (unsigned i = 0; i < count; ++i) {
            if (!records[i].ssid[0] || !strcmp((char *)records[i].ssid, ap_name)) continue;
            bool duplicate = false;
            for (unsigned j = 0; j < scan_result.count; ++j) {
                if (!strcmp((char *)records[i].ssid, scan_result.items[j].ssid)) { duplicate = true; break; }
            }
            if (duplicate) continue;
            network_scan_item_t *item = &scan_result.items[scan_result.count++];
            memcpy(item->ssid, records[i].ssid, 32);
            item->ssid[32] = 0;
            item->rssi = records[i].rssi;
            item->secure = records[i].authmode != WIFI_AUTH_OPEN;
        }
    }
    xSemaphoreGive(scan_lock);
    free(records);
    if (err != ESP_OK) esp_wifi_clear_ap_list();
    ESP_LOGI(TAG, "Wi-Fi scan completed: %u unique networks", scan_result.count);
}

static void synced(struct timeval *tv)
{
    (void)tv;
    xEventGroupSetBits(flags, SYNCED);
    ESP_LOGI(TAG, "Network time synchronized");
}

static void event_handler(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    (void)ctx;
    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        scan_finished();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START && settings_get()->ssid[0]) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(flags, ONLINE);
        const wifi_event_sta_disconnected_t *event = data;
        ESP_LOGW(TAG, "Wi-Fi disconnected, reason=%u; retrying without clearing settings", event->reason);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        xEventGroupSetBits(flags, ONLINE);
        ESP_LOGI(TAG, "Wi-Fi ready, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_ERROR_CHECK(esp_netif_sntp_start());
    }
}

static void enable_ap(void)
{
    if (xEventGroupGetBits(flags) & AP_ACTIVE) return;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    xEventGroupSetBits(flags, AP_ACTIVE);
    ESP_LOGI(TAG, "Configuration AP enabled: %s; http://192.168.4.1", ap_name);
}

static void network_task(void *arg)
{
    (void)arg;
    unsigned offline_seconds = 0, online_seconds = 0;
    while (true) {
        if (xEventGroupGetBits(flags) & ONLINE) {
            offline_seconds = 0;
            wifi_sta_list_t clients = {0};
            bool editing = (xEventGroupGetBits(flags) & AP_ACTIVE) &&
                esp_wifi_ap_get_sta_list(&clients) == ESP_OK && clients.num > 0;
            if (editing) online_seconds = 0;
            if (!editing && ++online_seconds >= 300 && (xEventGroupGetBits(flags) & AP_ACTIVE)) {
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                xEventGroupClearBits(flags, AP_ACTIVE);
                ESP_LOGI(TAG, "Setup AP closed; settings remain available at http://casescreen.local");
            }
        } else {
            online_seconds = 0;
            ++offline_seconds;
            if (offline_seconds >= 30) enable_ap();
            network_scan_result_t scan;
            network_scan_result(&scan);
            if (!scan.scanning && settings_get()->ssid[0] && offline_seconds % 10 == 0) {
                esp_err_t err = esp_wifi_connect();
                if (err != ESP_OK) ESP_LOGD(TAG, "Connect deferred: %s", esp_err_to_name(err));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void network_init(void)
{
    flags = xEventGroupCreate();
    assert(flags);
    scan_lock = xSemaphoreCreateMutex();
    assert(scan_lock);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    station = esp_netif_create_default_wifi_sta();
    assert(station);
    ESP_ERROR_CHECK(esp_netif_set_hostname(station, "casescreen"));
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    assert(ap_netif);
    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP, mac));
    snprintf(ap_name, sizeof(ap_name), "CaseScreen-%02X%02X", mac[4], mac[5]);
    snprintf(ap_password, sizeof(ap_password), "Screen%02X%02X", mac[4], mac[5]);
    wifi_config_t ap = { .ap = {
        .channel = 1, .max_connection = 2, .authmode = WIFI_AUTH_WPA2_PSK,
    } };
    strlcpy((char *)ap.ap.ssid, ap_name, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(ap_name);
    strlcpy((char *)ap.ap.password, ap_password, sizeof(ap.ap.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    wifi_config_t sta = {0};
    memcpy(sta.sta.ssid, settings_get()->ssid, strlen(settings_get()->ssid));
    memcpy(sta.sta.password, settings_get()->password, strlen(settings_get()->password));
    sta.sta.pmf_cfg.capable = true;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    esp_sntp_config_t ntp = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3,
        ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "ntp.tencent.com", "cn.pool.ntp.org"));
    ntp.start = false;
    ntp.sync_cb = synced;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&ntp));
    xEventGroupSetBits(flags, AP_ACTIVE);
    ESP_ERROR_CHECK(esp_wifi_start());
    if (xEventGroupGetBits(flags) & AP_ACTIVE) {
        ESP_LOGI(TAG, "Configuration AP enabled: %s; http://192.168.4.1", ap_name);
    }
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("casescreen"));
    ESP_ERROR_CHECK(mdns_instance_name_set("机箱副屏设置"));
    ESP_ERROR_CHECK(mdns_service_add("机箱副屏设置", "_http", "_tcp", 80, NULL, 0));
    BaseType_t created = xTaskCreate(network_task, "network_watch", 4096, NULL, 4, NULL);
    assert(created == pdPASS);
}

esp_err_t network_scan_start(void)
{
    xSemaphoreTake(scan_lock, portMAX_DELAY);
    if (scan_result.scanning) { xSemaphoreGive(scan_lock); return ESP_OK; }
    scan_result.scanning = true;
    scan_result.ready = false;
    xSemaphoreGive(scan_lock);
    const wifi_scan_config_t config = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {.min = 80, .max = 150},
    };
    esp_err_t err = esp_wifi_scan_start(&config, false);
    if (err != ESP_OK) {
        xSemaphoreTake(scan_lock, portMAX_DELAY);
        scan_result.scanning = false;
        xSemaphoreGive(scan_lock);
    }
    return err;
}

void network_scan_result(network_scan_result_t *result)
{
    xSemaphoreTake(scan_lock, portMAX_DELAY);
    *result = scan_result;
    xSemaphoreGive(scan_lock);
}

void network_status(network_status_t *status)
{
    memset(status, 0, sizeof(*status));
    EventBits_t bits = xEventGroupGetBits(flags);
    status->online = (bits & ONLINE) != 0;
    status->ap_active = (bits & AP_ACTIVE) != 0;
    status->time_synced = (bits & SYNCED) != 0;
    strlcpy(status->ap_name, ap_name, sizeof(status->ap_name));
    strlcpy(status->ap_password, ap_password, sizeof(status->ap_password));
    if (status->online) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(station, &ip) == ESP_OK) {
            snprintf(status->ip, sizeof(status->ip), IPSTR, IP2STR(&ip.ip));
        }
    }
}
