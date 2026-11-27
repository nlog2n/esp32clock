#include "brightness.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "nvs.h"

#define BRIGHTNESS_MIN 5
#define BRIGHTNESS_DEFAULT 40
#define BRIGHTNESS_NIGHT_MAX 10

static int s_percent = BRIGHTNESS_DEFAULT;
static int s_saved_percent = -1;
static bool s_night;

static esp_err_t apply_effective(void)
{
    int effective = s_night && s_percent > BRIGHTNESS_NIGHT_MAX ? BRIGHTNESS_NIGHT_MAX : s_percent;
    return bsp_display_brightness_set(effective);
}

int brightness_get_percent(void) { return s_percent; }

int brightness_set_percent(int percent)
{
    if (percent < BRIGHTNESS_MIN || percent > 100) return ESP_ERR_INVALID_ARG;
    int previous = s_percent;
    s_percent = percent;
    esp_err_t error = apply_effective();
    if (error != ESP_OK) s_percent = previous;
    return error;
}

void brightness_set_night(bool night)
{
    if (night == s_night) return;
    s_night = night;
    apply_effective();
}

int brightness_init(void)
{
    nvs_handle_t storage;
    uint8_t stored = BRIGHTNESS_DEFAULT;
    esp_err_t error = nvs_open("clock_display", NVS_READONLY, &storage);
    if (error == ESP_OK) {
        error = nvs_get_u8(storage, "brightness", &stored);
        nvs_close(storage);
    }
    if (error == ESP_OK && stored >= BRIGHTNESS_MIN && stored <= 100) s_saved_percent = stored;
    else if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND && error != ESP_ERR_NVS_PART_NOT_FOUND)
        ESP_LOGW("brightness", "Cannot read saved brightness: %s", esp_err_to_name(error));
    return brightness_set_percent(s_saved_percent >= BRIGHTNESS_MIN ? s_saved_percent : BRIGHTNESS_DEFAULT);
}

int brightness_save(void)
{
    if (s_saved_percent == s_percent) return ESP_OK;
    nvs_handle_t storage;
    esp_err_t error = nvs_open("clock_display", NVS_READWRITE, &storage);
    if (error != ESP_OK) return error;
    error = nvs_set_u8(storage, "brightness", (uint8_t)s_percent);
    if (error == ESP_OK) error = nvs_commit(storage);
    nvs_close(storage);
    if (error == ESP_OK) s_saved_percent = s_percent;
    return error;
}
