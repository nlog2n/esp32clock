#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    int minute;
    int previous_minute;
    uint64_t transition_start_ms;
    bool initialized;
    bool animating;
} flip_face_t;
bool flip_face_update(flip_face_t *face, int minute, uint64_t now_ms);
float flip_face_progress(const flip_face_t *face, uint64_t now_ms);
uint8_t flip_face_pixel(const flip_face_t *face, uint64_t now_ms, int x, int y);
/* Alpha of the 80x152 Gluqlo digit glyph at (x, y); 0 outside the glyph. */
uint8_t flip_face_digit_alpha(int digit, int x, int y);
