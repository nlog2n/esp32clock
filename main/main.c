/* Clock for ESP32-P4-Function-EV-Board (1024x600) with swipeable faces; display/touch come from the Espressif BSP + LVGL. */
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "app_config.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "clock_faces.h"
#include "clock_prefs.h"
#include "wifi_service.h"
#include "brightness.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "settings_ui.h"
#include "wifi_service.h"

#define FRAME_INTERVAL_MS 40
#define VALID_TIME_THRESHOLD 1700000000
#define NIGHT_START_HOUR 22
#define NIGHT_END_HOUR 7

static const char *TAG = "esp32clock";
static lv_obj_t *s_status;


static const char *time_status_text(void)
{
    wifi_service_snapshot_t net;
    if (wifi_service_read_snapshot(&net) != 0) return "Wi-Fi unavailable";
    switch (net.state) {
    case WIFI_SERVICE_CONNECTING: return "Connecting to Wi-Fi...";
    case WIFI_SERVICE_CONNECTED: return "Synchronizing time...";
    case WIFI_SERVICE_FAILED: return "Wi-Fi failed. Long-press to open Settings.";
    default: return "No Wi-Fi. Long-press to open Settings.";
    }
}

static void screen_long_pressed(lv_event_t *event)
{
    (void)event;
    settings_ui_open();
}

static void screen_gesture(lv_event_t *event)
{
    (void)event;
    if (settings_ui_is_open()) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_LEFT) clock_faces_step(1);
    else if (dir == LV_DIR_RIGHT) clock_faces_step(-1);
}

static void init_network_stack(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    time_t now = time(NULL);
    if (now < VALID_TIME_THRESHOLD) {
        if (!clock_faces_ready()) lv_label_set_text(s_status, time_status_text());
        return;
    }
    time_t local = now + clock_prefs_utc_offset_seconds();
    struct tm parts;
    if (!gmtime_r(&local, &parts)) return;
    bool first = !clock_faces_ready();
    clock_faces_update(&parts, app_uptime_ms());
    brightness_set_night(clock_prefs_auto_dim() &&
                         (parts.tm_hour >= NIGHT_START_HOUR || parts.tm_hour < NIGHT_END_HOUR));
    if (first) lv_label_set_text(s_status, "");
}

void app_main(void)
{
    init_network_stack();
    clock_prefs_load();
    bsp_display_start();
    bsp_display_lock(0);
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    if (!clock_faces_create(screen)) {
        bsp_display_unlock();
        return;
    }

    s_status = lv_label_create(screen);
    lv_label_set_text(s_status, "");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x8C8C8C), 0);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -16);

    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, screen_long_pressed, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(screen, screen_gesture, LV_EVENT_GESTURE, NULL);
    settings_ui_create(screen);
    lv_timer_create(tick, FRAME_INTERVAL_MS, NULL);
    bsp_display_unlock();
    if (brightness_init() != 0) bsp_display_backlight_on();
    esp_err_t err = wifi_service_start();
    if (err != ESP_OK) ESP_LOGE(TAG, "Wi-Fi/SNTP start failed: %s", esp_err_to_name(err));
}
