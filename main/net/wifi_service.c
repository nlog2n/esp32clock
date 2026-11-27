#include "wifi_service.h"
#include "sdkconfig.h"
#include "wifi_profiles.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#define CONNECT_TIMEOUT_US 20000000

typedef struct { bool scan; bool saved; wifi_profile_t profile; } wifi_command_t;
typedef struct {
    SemaphoreHandle_t lock;
    QueueHandle_t commands;
    esp_netif_t *netif;
    wifi_service_snapshot_t snapshot;
    wifi_profiles_t profiles;
    wifi_profile_t target;
    wifi_command_t command;
    wifi_config_t config;
    wifi_ap_record_t records[WIFI_SERVICE_RESULT_LIMIT];
    atomic_bool ip_ready;
    bool reconnect;
    int64_t attempt_started;
    int64_t next_retry;
} wifi_service_ctx_t;
/* One boot-lifetime station owner. UI receives copies, never driver handles. */
static wifi_service_ctx_t *s_service;

static void set_state(wifi_service_ctx_t *wifi, wifi_service_state_t state, const char *message)
{
    xSemaphoreTake(wifi->lock,portMAX_DELAY);
    wifi->snapshot.state=state;
    strlcpy(wifi->snapshot.message,message,sizeof(wifi->snapshot.message));
    strlcpy(wifi->snapshot.ssid,wifi->target.ssid,sizeof(wifi->snapshot.ssid));
    ++wifi->snapshot.revision;
    xSemaphoreGive(wifi->lock);
}
static void network_event(void *context, esp_event_base_t base, int32_t event, void *data)
{
    wifi_service_ctx_t *wifi=context;
    if (base==IP_EVENT && event==IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ip=data;
        if (ip && ip->esp_netif==wifi->netif && ip->ip_info.ip.addr) atomic_store(&wifi->ip_ready,true);
    } else if ((base==WIFI_EVENT && (event==WIFI_EVENT_STA_DISCONNECTED || event==WIFI_EVENT_STA_STOP)) ||
               (base==IP_EVENT && event==IP_EVENT_STA_LOST_IP)) {
        atomic_store(&wifi->ip_ready,false);
    }
}
static void load_profiles(wifi_service_ctx_t *wifi)
{
    nvs_handle_t storage;
    if (nvs_open("clock_wifi",NVS_READONLY,&storage)==ESP_OK) {
        size_t size=sizeof(wifi->profiles);
        if (nvs_get_blob(storage,"profiles",&wifi->profiles,&size)!=ESP_OK ||
            size!=sizeof(wifi->profiles) || wifi->profiles.count>WIFI_PROFILE_LIMIT) memset(&wifi->profiles,0,sizeof(wifi->profiles));
        nvs_close(storage);
    }
    for (unsigned i=0;i<wifi->profiles.count;++i) {
        wifi_profile_t *p=&wifi->profiles.entries[i];
        if (!memchr(p->ssid,0,sizeof(p->ssid)) || !memchr(p->password,0,sizeof(p->password)) ||
            !wifi_profile_credentials_valid(p->ssid,p->password)) { memset(&wifi->profiles,0,sizeof(wifi->profiles)); break; }
    }
    /* Import the build-time network once, if it is not already stored. */
    if (CONFIG_ESP32CLOCK_WIFI_SSID[0] && wifi_profile_find(&wifi->profiles,CONFIG_ESP32CLOCK_WIFI_SSID)<0 &&
        wifi_profile_credentials_valid(CONFIG_ESP32CLOCK_WIFI_SSID,CONFIG_ESP32CLOCK_WIFI_PASSWORD) &&
        wifi->profiles.count<WIFI_PROFILE_LIMIT) {
        wifi_profile_t *seed=&wifi->profiles.entries[wifi->profiles.count++];
        strlcpy(seed->ssid,CONFIG_ESP32CLOCK_WIFI_SSID,sizeof(seed->ssid));
        strlcpy(seed->password,CONFIG_ESP32CLOCK_WIFI_PASSWORD,sizeof(seed->password));
    }
    memset(&wifi->target,0,sizeof(wifi->target));
    if (wifi->profiles.count) wifi->target=wifi->profiles.entries[0];
}
static esp_err_t save_profile(wifi_service_ctx_t *wifi)
{
    wifi_profile_remember(&wifi->profiles,wifi->target.ssid,wifi->target.password);
    nvs_handle_t storage;
    esp_err_t error=nvs_open("clock_wifi",NVS_READWRITE,&storage);
    if (error!=ESP_OK) return error;
    error=nvs_set_blob(storage,"profiles",&wifi->profiles,sizeof(wifi->profiles));
    if (error==ESP_OK) error=nvs_commit(storage);
    nvs_close(storage);
    return error;
}
static void connect_target(wifi_service_ctx_t *wifi)
{
    /* Stop resets the previous association before credentials change. */
    esp_err_t error=esp_wifi_stop();
    if (error==ESP_ERR_WIFI_NOT_STARTED) error=ESP_OK;
    atomic_store(&wifi->ip_ready,false);
    memset(&wifi->config,0,sizeof(wifi->config));
    memcpy(wifi->config.sta.ssid,wifi->target.ssid,strlen(wifi->target.ssid));
    memcpy(wifi->config.sta.password,wifi->target.password,strlen(wifi->target.password));
    wifi->config.sta.pmf_cfg.capable=true;
    wifi->config.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
    if (error==ESP_OK) error=esp_wifi_set_config(WIFI_IF_STA,&wifi->config);
    if (error==ESP_OK) error=esp_wifi_start();
    if (error==ESP_OK) error=esp_wifi_connect();
    memset(&wifi->config,0,sizeof(wifi->config));
    wifi->attempt_started=esp_timer_get_time();
    set_state(wifi,error==ESP_OK ? WIFI_SERVICE_CONNECTING : WIFI_SERVICE_FAILED,
              error==ESP_OK ? "Connecting..." : "Could not connect. Tap the network to retry.");
}
static bool supported_auth(wifi_auth_mode_t mode)
{
    return mode==WIFI_AUTH_OPEN || mode==WIFI_AUTH_WPA_PSK || mode==WIFI_AUTH_WPA2_PSK ||
           mode==WIFI_AUTH_WPA_WPA2_PSK || mode==WIFI_AUTH_WPA3_PSK || mode==WIFI_AUTH_WPA2_WPA3_PSK;
}
static void scan_networks(wifi_service_ctx_t *wifi)
{
    if (wifi->snapshot.state==WIFI_SERVICE_CONNECTING) {
        (void)esp_wifi_disconnect();
        wifi->reconnect=false;
        set_state(wifi,WIFI_SERVICE_IDLE,"Choose a network");
    }
    xSemaphoreTake(wifi->lock,portMAX_DELAY);
    wifi->snapshot.scanning=true;
    ++wifi->snapshot.revision;
    xSemaphoreGive(wifi->lock);
    esp_err_t error=esp_wifi_scan_start(NULL,true);
    uint16_t count=WIFI_SERVICE_RESULT_LIMIT;
    if (error==ESP_OK) error=esp_wifi_scan_get_ap_records(&count,wifi->records);
    if (error!=ESP_OK) { (void)esp_wifi_clear_ap_list(); count=0; }
    xSemaphoreTake(wifi->lock,portMAX_DELAY);
    wifi->snapshot.count=0;
    for (uint16_t i=0;i<count;++i) {
        wifi_ap_record_t *record=&wifi->records[i];
        if (!record->ssid[0]) continue;
        bool duplicate=false;
        for (size_t j=0;j<wifi->snapshot.count;++j)
            if (!strncmp(wifi->snapshot.access_points[j].ssid,(char *)record->ssid,33)) duplicate=true;
        if (duplicate) continue;
        wifi_service_ap_t *ap=&wifi->snapshot.access_points[wifi->snapshot.count++];
        strlcpy(ap->ssid,(char *)record->ssid,sizeof(ap->ssid));
        ap->rssi=record->rssi;
        ap->secured=record->authmode!=WIFI_AUTH_OPEN;
        ap->supported=supported_auth(record->authmode);
        ap->saved=wifi_profile_find(&wifi->profiles,ap->ssid)>=0;
    }
    wifi->snapshot.scanning=false;
    if (error!=ESP_OK) strlcpy(wifi->snapshot.message,"Scan failed. Tap Refresh to retry.",sizeof(wifi->snapshot.message));
    ++wifi->snapshot.revision;
    xSemaphoreGive(wifi->lock);
}
static void network_task(void *argument)
{
    wifi_service_ctx_t *wifi=argument;
    load_profiles(wifi);
    if (wifi->target.ssid[0]) { wifi->reconnect=true; connect_target(wifi); }
    for (;;) {
        if (xQueueReceive(wifi->commands,&wifi->command,pdMS_TO_TICKS(100))==pdTRUE) {
            if (wifi->command.scan) scan_networks(wifi);
            else {
                int saved=wifi_profile_find(&wifi->profiles,wifi->command.profile.ssid);
                if (wifi->command.saved && saved<0) set_state(wifi,WIFI_SERVICE_FAILED,"Saved password unavailable. Enter it again.");
                else {
                    wifi->target=wifi->command.saved ? wifi->profiles.entries[saved] : wifi->command.profile;
                    wifi->reconnect=false;
                    connect_target(wifi);
                }
            }
            memset(&wifi->command,0,sizeof(wifi->command));
        }
        wifi_service_state_t state=wifi->snapshot.state;
        bool associated=esp_wifi_sta_get_ap_info(&wifi->records[0])==ESP_OK &&
                        !strcmp((char *)wifi->records[0].ssid,wifi->target.ssid);
        if (state==WIFI_SERVICE_CONNECTING && associated && atomic_load(&wifi->ip_ready)) {
            esp_err_t error=save_profile(wifi);
            wifi->reconnect=true;
            xSemaphoreTake(wifi->lock,portMAX_DELAY);
            for (size_t i=0;i<wifi->snapshot.count;++i)
                wifi->snapshot.access_points[i].saved=wifi_profile_find(&wifi->profiles,wifi->snapshot.access_points[i].ssid)>=0;
            xSemaphoreGive(wifi->lock);
            set_state(wifi,WIFI_SERVICE_CONNECTED,error==ESP_OK ? "Connected" : "Connected, but could not save password.");
        } else if (state==WIFI_SERVICE_CONNECTING && esp_timer_get_time()-wifi->attempt_started>CONNECT_TIMEOUT_US) {
            (void)esp_wifi_disconnect();
            wifi->next_retry=esp_timer_get_time()+5000000;
            set_state(wifi,WIFI_SERVICE_FAILED,"Connection failed. Check password and retry.");
        } else if (state==WIFI_SERVICE_CONNECTED && (!associated || !atomic_load(&wifi->ip_ready))) {
            wifi->next_retry=esp_timer_get_time()+5000000;
            set_state(wifi,WIFI_SERVICE_IDLE,"Disconnected. Reconnecting...");
        } else if ((state==WIFI_SERVICE_IDLE || state==WIFI_SERVICE_FAILED) && wifi->reconnect && esp_timer_get_time()>=wifi->next_retry) connect_target(wifi);
    }
}
int wifi_service_request_scan(void)
{
    if (!s_service) return ESP_ERR_INVALID_STATE;
    wifi_command_t command={.scan=true};
    return xQueueSend(s_service->commands,&command,0)==pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
int wifi_service_request_connect(const char *ssid,const char *password)
{
    if (!s_service || !ssid || !ssid[0] || strlen(ssid)>32 || (password && !wifi_profile_credentials_valid(ssid,password))) return ESP_ERR_INVALID_ARG;
    wifi_command_t command={.saved=password==NULL};
    strlcpy(command.profile.ssid,ssid,sizeof(command.profile.ssid));
    if (password) strlcpy(command.profile.password,password,sizeof(command.profile.password));
    int error=xQueueSend(s_service->commands,&command,0)==pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
    memset(&command,0,sizeof(command));
    return error;
}
int wifi_service_read_snapshot(wifi_service_snapshot_t *snapshot)
{
    if (!s_service || !snapshot) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_service->lock,portMAX_DELAY);
    *snapshot=s_service->snapshot;
    xSemaphoreGive(s_service->lock);
    return ESP_OK;
}
esp_err_t wifi_service_start(void)
{
    if (s_service) return ESP_ERR_INVALID_STATE;
    wifi_service_ctx_t *wifi=calloc(1,sizeof(*wifi));
    if (!wifi) return ESP_ERR_NO_MEM;
    wifi->lock=xSemaphoreCreateMutex();
    wifi->commands=xQueueCreate(1,sizeof(wifi_command_t));
    esp_err_t error=ESP_ERR_NO_MEM;
    bool wifi_initialized=false, sntp_initialized=false;
    esp_event_handler_instance_t wifi_handler=NULL, ip_handler=NULL;
    if (!wifi->lock || !wifi->commands) goto fail;
    error=esp_netif_init(); if (error!=ESP_OK) goto fail;
    error=esp_event_loop_create_default(); if (error!=ESP_OK && error!=ESP_ERR_INVALID_STATE) goto fail;
    wifi->netif=esp_netif_create_default_wifi_sta();
    if (!wifi->netif) { error=ESP_ERR_NO_MEM; goto fail; }
    wifi_init_config_t *init=calloc(1,sizeof(*init));
    if (!init) { error=ESP_ERR_NO_MEM; goto fail; }
    *init=(wifi_init_config_t)WIFI_INIT_CONFIG_DEFAULT();
    error=esp_wifi_init(init); free(init); if (error!=ESP_OK) goto fail;
    wifi_initialized=true;
    error=esp_wifi_set_storage(WIFI_STORAGE_RAM); if (error!=ESP_OK) goto fail;
    error=esp_wifi_set_mode(WIFI_MODE_STA); if (error!=ESP_OK) goto fail;
    error=esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,network_event,wifi,&wifi_handler); if (error!=ESP_OK) goto fail;
    error=esp_event_handler_instance_register(IP_EVENT,ESP_EVENT_ANY_ID,network_event,wifi,&ip_handler); if (error!=ESP_OK) goto fail;
    esp_sntp_config_t config=ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_ESP32CLOCK_NTP_SERVER);
    error=esp_netif_sntp_init(&config); if (error!=ESP_OK) goto fail;
    sntp_initialized=true;
    error=esp_wifi_start(); if (error!=ESP_OK) goto fail;
    strlcpy(wifi->snapshot.message,"Choose a network",sizeof(wifi->snapshot.message));
    s_service=wifi;
    if (xTaskCreate(network_task,"clock_wifi",4096,wifi,4,NULL)!=pdPASS) { s_service=NULL; error=ESP_ERR_NO_MEM; goto fail; }
    return ESP_OK;
fail:
    if (wifi_handler) esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_handler);
    if (ip_handler) esp_event_handler_instance_unregister(IP_EVENT,ESP_EVENT_ANY_ID,ip_handler);
    if (sntp_initialized) esp_netif_sntp_deinit();
    if (wifi_initialized) { (void)esp_wifi_stop(); esp_wifi_deinit(); }
    if (wifi->netif) esp_netif_destroy_default_wifi(wifi->netif);
    if (wifi->commands) vQueueDelete(wifi->commands);
    if (wifi->lock) vSemaphoreDelete(wifi->lock);
    free(wifi);
    return error;
}
