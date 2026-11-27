#include "flip_face.h"
#include "digit_masks.h"

#define FLIP_DURATION_MS 600U
#define HALF_DURATION_MS (FLIP_DURATION_MS / 2U)
#define CARD_WIDTH 216
#define CARD_HEIGHT 224
#define HALF_HEIGHT (CARD_HEIGHT / 2)

uint8_t flip_face_digit_alpha(int digit, int x, int y)
{
    if (digit < 0 || digit > 9 || x < 0 || x >= 80 || y < 0 || y >= 152) return 0;
    return digit_masks[digit][y * 80 + x];
}

bool flip_face_update(flip_face_t *face, int minute, uint64_t now_ms)
{
    if (!face || minute < 0 || minute >= 1440) return false;
    if (!face->initialized) {
        face->minute = face->previous_minute = minute;
        face->initialized = true;
        return true;
    }
    if (minute != face->minute) {
        face->previous_minute = face->minute;
        face->minute = minute;
        face->transition_start_ms = now_ms;
        face->animating = true;
        return true;
    }
    if (face->animating) {
        if (now_ms - face->transition_start_ms >= FLIP_DURATION_MS)
            face->animating = false;
        return true;
    }
    return false;
}

float flip_face_progress(const flip_face_t *face, uint64_t now_ms)
{
    if (!face->animating) return 1.0f;
    uint64_t elapsed = now_ms - face->transition_start_ms;
    return elapsed >= FLIP_DURATION_MS ? 1.0f : (float)elapsed / FLIP_DURATION_MS;
}

static uint8_t card_pixel(int value, int x, int y)
{
    if (x < 0 || x >= CARD_WIDTH || y < 0 || y >= CARD_HEIGHT) return 0;
    int corner_x = x < 12 ? 12 - x : x >= CARD_WIDTH - 12 ? x - (CARD_WIDTH - 13) : 0;
    int corner_y = y < 12 ? 12 - y : y >= CARD_HEIGHT - 12 ? y - (CARD_HEIGHT - 13) : 0;
    if (corner_x && corner_y && corner_x * corner_x + corner_y * corner_y > 144) return 0;
    uint8_t background = y < HALF_HEIGHT ? 23 : 30;
    if (value < 0) {
        return y >= 108 && y < 116 && x >= 40 && x < 176 ? 140 : background;
    }
    int digit_x = x - 28;
    int digit_y = y - 36;
    if (digit_x >= 0 && digit_x < 160 && digit_y >= 0 && digit_y < 152) {
        int digit = digit_x < 80 ? value / 10 : value % 10;
        unsigned alpha = digit_masks[digit][digit_y * 80 + digit_x % 80];
        unsigned white = y < HALF_HEIGHT ? 224 : 246;
        return background + (white - background) * alpha / 255;
    }
    return background;
}

uint8_t flip_face_pixel(const flip_face_t *face, uint64_t now_ms, int x, int y)
{
    if (!face || x < 0 || x >= 448 || y < 0 || y >= CARD_HEIGHT) return 0;
    bool minutes = x >= 232;
    if (x >= CARD_WIDTH && !minutes) return 0;
    if (minutes) x -= 232;
    int current = !face->initialized ? -1 : minutes ? face->minute % 60 : face->minute / 60;
    int previous = !face->initialized ? -1 : minutes ? face->previous_minute % 60 : face->previous_minute / 60;
    uint64_t elapsed = now_ms - face->transition_start_ms;
    uint8_t color;
    if (!face->animating || current == previous || elapsed >= FLIP_DURATION_MS) {
        color = card_pixel(current, x, y);
    } else {
        color = card_pixel(y < HALF_HEIGHT ? current : previous, x, y);
        if (elapsed < HALF_DURATION_MS) {
            int height = HALF_HEIGHT * (HALF_DURATION_MS - elapsed) / HALF_DURATION_MS;
            if (height && y < HALF_HEIGHT && y >= HALF_HEIGHT - height) {
                int source_y = (y - (HALF_HEIGHT - height)) * HALF_HEIGHT / height;
                color = card_pixel(previous, x, source_y) * (180 + 75 * height / HALF_HEIGHT) / 255;
            }
        } else {
            int height = HALF_HEIGHT * (elapsed - HALF_DURATION_MS) / HALF_DURATION_MS;
            if (height && y >= HALF_HEIGHT && y < HALF_HEIGHT + height) {
                int source_y = HALF_HEIGHT + (y - HALF_HEIGHT) * HALF_HEIGHT / height;
                color = card_pixel(current, x, source_y) * (180 + 75 * height / HALF_HEIGHT) / 255;
            }
        }
    }
    if (y == HALF_HEIGHT - 1 || y == HALF_HEIGHT) color = 3;
    return color;
}
