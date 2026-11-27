#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "lvgl.h"

/* Builds every clock face on `screen` (LVGL lock held) and shows the saved one. */
bool clock_faces_create(lv_obj_t *screen);
/* Feed local broken-down time; cheap to call every frame. */
void clock_faces_update(const struct tm *local, uint64_t frame_ms);
/* True once a valid time has been drawn. */
bool clock_faces_ready(void);
/* Switch face by delta (+1 next, -1 previous, wraps) and persist the choice. */
void clock_faces_step(int delta);
/* Show a specific face and persist it. */
void clock_faces_select(int face);
