#pragma once
#include "lvgl.h"
/* Builds the settings pages on top of `screen`. Call with the LVGL lock held. */
void settings_ui_create(lv_obj_t *screen);
void settings_ui_open(void);
bool settings_ui_is_open(void);
