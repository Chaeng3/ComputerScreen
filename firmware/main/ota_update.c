#include <stdlib.h>
#include <string.h>
#include "ota_update.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PREFIX_SIZE (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))
#define CHUNK_SIZE 4096
static bool *restart_pending;
static const char *TAG = "ota";

static bool pending_validation(void)
{
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY;
}

static void response_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
}

static esp_err_t firmware_info(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_FAIL;
    cJSON_AddStringToObject(root, "version", app->version);
    cJSON_AddStringToObject(root, "partition", running->label);
    cJSON_AddStringToObject(root, "built", app->date);
    cJSON_AddNumberToObject(root, "max_size", next ? next->size : 0);
    cJSON_AddBoolToObject(root, "validating", pending_validation());
    cJSON_AddBoolToObject(root, "restarting", *restart_pending);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return ESP_FAIL;
    response_headers(req);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    esp_err_t result = httpd_resp_sendstr(req, body);
    cJSON_free(body);
    return result;
}

static bool valid_prefix(const uint8_t *bytes)
{
    const esp_image_header_t *header = (const esp_image_header_t *)bytes;
    const esp_app_desc_t *image = (const esp_app_desc_t *)(bytes + sizeof(*header) + sizeof(esp_image_segment_header_t));
    const esp_app_desc_t *running = esp_app_get_description();
    // Reject merged images, bootloaders, other boards/projects and pre-OTA releases
    // before erasing anything. The SDK validates the complete image at ota_end.
    if (header->magic != ESP_IMAGE_HEADER_MAGIC || header->chip_id != ESP_CHIP_ID_ESP32S3 ||
        header->hash_appended != 1 || header->segment_count < 1 || header->segment_count > ESP_IMAGE_MAX_SEGMENTS ||
        image->magic_word != ESP_APP_DESC_MAGIC_WORD || !memchr(image->project_name, 0, sizeof(image->project_name)) ||
        strcmp(image->project_name, running->project_name) || !memchr(image->version, 0, sizeof(image->version))) return false;
    size_t length = strlen(image->version);
    return length > 4 && !strcmp(image->version + length - 4, "-ota");
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

static esp_err_t upload(httpd_req_t *req)
{
    response_headers(req);
    char marker[8], type[64];
    if (httpd_req_get_hdr_value_str(req, "X-Case-Setup", marker, sizeof(marker)) != ESP_OK || strcmp(marker, "1") ||
        httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
        strcmp(type, "application/octet-stream"))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "请通过副屏设置页上传升级文件");
    if (*restart_pending || pending_validation())
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "设备正在重启或验证新版本，请稍后刷新再试");
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target || target->address == esp_ota_get_running_partition()->address)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "缺少备用升级分区，需要通过 USB 安装支持升级的版本");
    if (req->content_len < PREFIX_SIZE + 32 || req->content_len > target->size)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "文件大小不正确，请选择本项目的 OTA 升级文件");
    uint8_t *buffer = malloc(CHUNK_SIZE);
    if (!buffer) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "设备繁忙，请稍后重试");
    size_t received = 0;
    while (received < PREFIX_SIZE) {
        int count = httpd_req_recv(req, (char *)buffer + received, PREFIX_SIZE - received);
        if (count <= 0) { free(buffer); return httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "上传中断，原版本仍可使用"); }
        received += count;
    }
    if (!valid_prefix(buffer)) {
        free(buffer);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "不是兼容的副屏 OTA 固件，请选择 case_display-版本号-ota.bin");
    }
    esp_ota_handle_t handle = 0;
    esp_err_t error = esp_ota_begin(target, req->content_len, &handle);
    if (error != ESP_OK) {
        free(buffer);
        ESP_LOGE(TAG, "Begin failed: %s", esp_err_to_name(error));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "无法准备备用分区，原版本仍可使用");
    }
    error = esp_ota_write(handle, buffer, received);
    int64_t started = esp_timer_get_time();
    while (error == ESP_OK && received < req->content_len) {
        size_t wanted = req->content_len - received;
        if (wanted > CHUNK_SIZE) wanted = CHUNK_SIZE;
        int count = httpd_req_recv(req, (char *)buffer, wanted);
        if (count <= 0 || esp_timer_get_time() - started > 300000000) { error = ESP_ERR_TIMEOUT; break; }
        error = esp_ota_write(handle, buffer, count);
        received += count;
    }
    free(buffer);
    if (error != ESP_OK) {
        esp_ota_abort(handle);
        ESP_LOGW(TAG, "Upload aborted after %u bytes: %s", (unsigned)received, esp_err_to_name(error));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "上传未完成，原版本仍可使用，请重新上传");
    }
    error = esp_ota_end(handle); // Includes chip/revision, checksum and appended SHA-256 checks.
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "Image rejected: %s", esp_err_to_name(error));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "固件完整性校验失败，原版本仍可使用");
    }
    error = esp_ota_set_boot_partition(target);
    if (error != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "固件已写入，但未能切换启动版本，请重试");
    *restart_pending = true;
    ESP_LOGI(TAG, "Verified %u bytes; next boot: %s", (unsigned)received, target->label);
    if (xTaskCreate(restart_task, "ota_restart", 2048, NULL, 3, NULL) != pdPASS)
        return httpd_resp_sendstr(req, "固件校验通过。请重新上电完成升级，配置已保留。");
    return httpd_resp_sendstr(req, "固件校验通过，正在重启。请等待约 30 秒后刷新页面，Wi-Fi 和天气设置均保留。");
}

void ota_update_register(httpd_handle_t server, bool *reboot_pending)
{
    restart_pending = reboot_pending;
    const httpd_uri_t routes[] = {
        {.uri="/api/firmware", .method=HTTP_GET, .handler=firmware_info},
        {.uri="/api/update", .method=HTTP_POST, .handler=upload},
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i)
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
    ESP_LOGI(TAG, "Version %s, running %s", esp_app_get_description()->version, esp_ota_get_running_partition()->label);
}

void ota_update_health_tick(void)
{
    static bool done;
    static int64_t last_probe;
    if (done) return;
    if (!pending_validation()) { done = true; return; }
    int64_t now = esp_timer_get_time();
    if (now < 20000000 || now - last_probe < 5000000) return;
    last_probe = now;
    // Called only after settings, display, networking, portal and clock rendering
    // have initialized. Probe our own server; Internet/NTP availability is irrelevant.
    esp_http_client_config_t config = {.url="http://127.0.0.1/api/firmware", .timeout_ms=2000};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    bool healthy = false;
    if (client) {
        healthy = esp_http_client_perform(client) == ESP_OK && esp_http_client_get_status_code(client) == 200;
        esp_http_client_cleanup(client);
    }
    if (healthy && esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        done = true;
        ESP_LOGI(TAG, "Startup and web health check passed; version confirmed");
    } else if (now > 60000000 && esp_ota_check_rollback_is_possible()) {
        ESP_LOGE(TAG, "Startup web health check failed; rolling back");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}
