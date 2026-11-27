#include "clock_prefs.h"
#include "esp_log.h"
#include "nvs.h"

#define NAMESPACE "clock_prefs"
#define DEFAULT_ZONE 9 /* Singapore / Beijing, matches the original UTC+8 */

/* Fixed offsets: daylight saving is not applied. */
const clock_prefs_zone_t clock_prefs_zones[CLOCK_PREFS_ZONE_COUNT] = {
    {"UTC-08:00  Los Angeles", -8 * 60},
    {"UTC-06:00  Chicago", -6 * 60},
    {"UTC-05:00  New York", -5 * 60},
    {"UTC-03:00  Sao Paulo", -3 * 60},
    {"UTC+00:00  London", 0},
    {"UTC+01:00  Paris / Berlin", 60},
    {"UTC+03:00  Moscow", 3 * 60},
    {"UTC+04:00  Dubai", 4 * 60},
    {"UTC+05:30  Delhi", 5 * 60 + 30},
    {"UTC+08:00  Singapore / Beijing", 8 * 60},
    {"UTC+09:00  Tokyo / Seoul", 9 * 60},
    {"UTC+10:00  Sydney", 10 * 60},
};

static int s_face = CLOCK_FACE_FLIP;
static int s_zone = DEFAULT_ZONE;
static bool s_auto_dim;

static void store_u8(const char *key, uint8_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, key, value);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err != ESP_OK) ESP_LOGW("prefs", "save %s failed: %s", key, esp_err_to_name(err));
}

void clock_prefs_load(void)
{
    nvs_handle_t handle;
    if (nvs_open(NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    uint8_t value;
    if (nvs_get_u8(handle, "face", &value) == ESP_OK && value < CLOCK_FACE_COUNT) s_face = value;
    if (nvs_get_u8(handle, "zone", &value) == ESP_OK && value < CLOCK_PREFS_ZONE_COUNT) s_zone = value;
    if (nvs_get_u8(handle, "autodim", &value) == ESP_OK) s_auto_dim = value != 0;
    nvs_close(handle);
}

int clock_prefs_face(void) { return s_face; }
int clock_prefs_zone(void) { return s_zone; }
bool clock_prefs_auto_dim(void) { return s_auto_dim; }
int clock_prefs_utc_offset_seconds(void) { return clock_prefs_zones[s_zone].offset_minutes * 60; }

void clock_prefs_set_face(int face)
{
    if (face < 0 || face >= CLOCK_FACE_COUNT || face == s_face) return;
    s_face = face;
    store_u8("face", (uint8_t)face);
}

void clock_prefs_set_zone(int zone)
{
    if (zone < 0 || zone >= CLOCK_PREFS_ZONE_COUNT || zone == s_zone) return;
    s_zone = zone;
    store_u8("zone", (uint8_t)zone);
}

void clock_prefs_set_auto_dim(bool enabled)
{
    if (enabled == s_auto_dim) return;
    s_auto_dim = enabled;
    store_u8("autodim", enabled ? 1 : 0);
}
