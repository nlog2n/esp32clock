#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define WIFI_SERVICE_RESULT_LIMIT 32

typedef enum { WIFI_SERVICE_IDLE, WIFI_SERVICE_CONNECTING, WIFI_SERVICE_CONNECTED, WIFI_SERVICE_FAILED } wifi_service_state_t;
typedef struct {
    char ssid[33];
    int rssi;
    bool secured;
    bool saved;
    bool supported;
} wifi_service_ap_t;
typedef struct {
    wifi_service_state_t state;
    bool scanning;
    unsigned revision;
    size_t count;
    char ssid[33];
    char message[80];
    wifi_service_ap_t access_points[WIFI_SERVICE_RESULT_LIMIT];
} wifi_service_snapshot_t;
/* Nonblocking commands. NULL password selects stored credentials. Never logs secrets. */
int wifi_service_request_scan(void);
int wifi_service_request_connect(const char *ssid, const char *password);
/* Copies a mutex-consistent snapshot; no driver or storage operations. */
int wifi_service_read_snapshot(wifi_service_snapshot_t *snapshot);
/* Starts the Wi-Fi station owner task and SNTP. Requires NVS, netif and the
 * default event loop to be initialized by the caller. */
esp_err_t wifi_service_start(void);
