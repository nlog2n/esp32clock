#pragma once
#include <stdbool.h>
/* Call after bsp_display_start(). Runtime calls belong to the LVGL task. */
int brightness_init(void);
int brightness_get_percent(void);
/* Applies immediately; valid range 5..100. Returns zero on success. */
int brightness_set_percent(int percent);
/* Caps the panel at 10% while night is true (the stored value is unchanged). */
void brightness_set_night(bool night);
/* Persists the applied value, on slider release or Back. */
int brightness_save(void);
