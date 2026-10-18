#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define NETWORK_SCAN_MAX 32
typedef struct {
    char ssid[33];
    int rssi;
    bool secure;
} network_scan_item_t;
typedef struct {
    bool scanning;
    bool ready;
    unsigned count;
    network_scan_item_t items[NETWORK_SCAN_MAX];
} network_scan_result_t;

typedef struct {
    bool online;
    bool ap_active;
    bool time_synced;
    char ip[16];
    char ap_name[33];
    char ap_password[16];
} network_status_t;

void network_init(void);
void network_status(network_status_t *status);
esp_err_t network_scan_start(void);
void network_scan_result(network_scan_result_t *result);
