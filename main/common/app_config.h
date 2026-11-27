#pragma once
#include <stdint.h>
#include "esp_timer.h"

/* Panel geometry of the 7" ESP32-P4-HMI-SubBoard. */
#define APP_SCREEN_W 1024
#define APP_SCREEN_H 600

/* Milliseconds since boot (monotonic). */
static inline uint64_t app_uptime_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000); }
