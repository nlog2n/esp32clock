#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { CLOCK_FACE_FLIP, CLOCK_FACE_DIGITAL, CLOCK_FACE_CLASSIC, CLOCK_FACE_DIVER, CLOCK_FACE_COUNT } clock_face_t;

typedef struct { const char *name; int offset_minutes; } clock_prefs_zone_t;

#define CLOCK_PREFS_ZONE_COUNT 12
extern const clock_prefs_zone_t clock_prefs_zones[CLOCK_PREFS_ZONE_COUNT];

/* Loads values from NVS (defaults when absent). Call once after nvs_flash_init(). */
void clock_prefs_load(void);

int clock_prefs_face(void);
int clock_prefs_zone(void);
bool clock_prefs_auto_dim(void);
/* UTC offset of the selected zone in seconds. */
int clock_prefs_utc_offset_seconds(void);

/* Each setter validates, updates and persists. */
void clock_prefs_set_face(int face);
void clock_prefs_set_zone(int zone);
void clock_prefs_set_auto_dim(bool enabled);
