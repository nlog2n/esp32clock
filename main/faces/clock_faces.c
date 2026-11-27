/* Clock faces for the 1024x600 panel: Flip (Gluqlo), Digital, Classic and Diver.
 * Swipe left/right (handled by main) steps through them. All calls run in the
 * LVGL task with the display lock held. */
#include "clock_faces.h"
#include "app_config.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "clock_prefs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "flip_face.h"

#define CX (APP_SCREEN_W / 2)
#define CY (APP_SCREEN_H / 2)
#define DIAL_R 272
#define FLIP_W 448
#define FLIP_H 224
#define DIGITAL_W 360
#define DIGITAL_H 152
#define SCALE_2X 512
#define DOTS_VISIBLE_MS 2000
#define PI_F 3.14159265f

#define COLOR_ORANGE lv_color_hex(0xFF7A00)
#define COLOR_LUME lv_color_hex(0xB8FFD0)
#define COLOR_TICK lv_color_hex(0x777777)

typedef struct {
    lv_obj_t *root, *hour, *minute, *second, *date;
    lv_point_precise_t hour_pts[2], minute_pts[2], second_pts[2];
    lv_point_precise_t ticks[60][2];
} analog_t;

static lv_obj_t *s_roots[CLOCK_FACE_COUNT];
static lv_obj_t *s_dots[CLOCK_FACE_COUNT];
static lv_obj_t *s_dots_box;
static lv_obj_t *s_flip_canvas, *s_digital_canvas, *s_digital_date;
static uint16_t *s_flip_pixels, *s_digital_pixels;
static flip_face_t s_flip;
static uint64_t s_flip_frame_ms, s_dots_until_ms;
static analog_t s_classic, s_diver;
static int s_face = CLOCK_FACE_FLIP;
static int32_t s_key = -1;
static bool s_ready;


static uint16_t rgb565_gray(uint8_t g) { return (uint16_t)(((g >> 3) << 11) | ((g >> 2) << 5) | (g >> 3)); }

static lv_obj_t *make_plain(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *make_fullscreen(lv_obj_t *parent)
{
    lv_obj_t *obj = make_plain(parent);
    lv_obj_set_size(obj, APP_SCREEN_W, APP_SCREEN_H);
    lv_obj_set_pos(obj, 0, 0);
    return obj;
}

static lv_obj_t *make_circle(lv_obj_t *parent, int radius, lv_color_t fill, int border_w, lv_color_t border)
{
    lv_obj_t *obj = make_plain(parent);
    lv_obj_set_size(obj, radius * 2, radius * 2);
    lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj, fill, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, border_w, 0);
    lv_obj_set_style_border_color(obj, border, 0);
    return obj;
}

static lv_obj_t *make_line(lv_obj_t *parent, int width, lv_color_t color)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_color(line, color, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    return line;
}

static lv_obj_t *make_text(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

/* Angle in degrees, 0 = 12 o'clock, clockwise. */
static void polar(float radius, float deg, float *x, float *y)
{
    float rad = deg * PI_F / 180.0f;
    *x = CX + radius * sinf(rad);
    *y = CY - radius * cosf(rad);
}

static void set_hand(lv_obj_t *line, lv_point_precise_t pts[2], float deg, float tail, float length)
{
    float x, y;
    polar(-tail, deg, &x, &y);
    pts[0].x = x; pts[0].y = y;
    polar(length, deg, &x, &y);
    pts[1].x = x; pts[1].y = y;
    lv_line_set_points_mutable(line, pts, 2);
}

static void build_ticks(analog_t *dial, float outer, float hour_len, float minute_len, int hour_w, int minute_w,
                        lv_color_t hour_color, lv_color_t minute_color)
{
    for (int i = 0; i < 60; ++i) {
        bool hour = i % 5 == 0;
        float x1, y1, x2, y2;
        polar(outer, i * 6.0f, &x2, &y2);
        polar(outer - (hour ? hour_len : minute_len), i * 6.0f, &x1, &y1);
        dial->ticks[i][0].x = x1; dial->ticks[i][0].y = y1;
        dial->ticks[i][1].x = x2; dial->ticks[i][1].y = y2;
        lv_obj_t *line = make_line(dial->root, hour ? hour_w : minute_w, hour ? hour_color : minute_color);
        lv_line_set_points(line, dial->ticks[i], 2);
    }
}

static void build_classic(lv_obj_t *screen)
{
    analog_t *d = &s_classic;
    d->root = make_fullscreen(screen);
    make_circle(d->root, DIAL_R, lv_color_black(), 4, lv_color_hex(0x3A3A3A));
    build_ticks(d, DIAL_R - 8, 30, 14, 6, 2, lv_color_white(), COLOR_TICK);
    for (int h = 1; h <= 12; ++h) {
        char text[4];
        snprintf(text, sizeof(text), "%d", h);
        lv_obj_t *label = make_text(d->root, text, &lv_font_montserrat_32, lv_color_white());
        float x, y;
        polar(DIAL_R - 70, h * 30.0f, &x, &y);
        lv_obj_align(label, LV_ALIGN_CENTER, (int)(x - CX), (int)(y - CY));
    }
    d->hour = make_line(d->root, 12, lv_color_white());
    d->minute = make_line(d->root, 8, lv_color_white());
    d->second = make_line(d->root, 3, COLOR_ORANGE);
    make_circle(d->root, 11, COLOR_ORANGE, 0, COLOR_ORANGE);
}

static void build_diver(lv_obj_t *screen)
{
    analog_t *d = &s_diver;
    d->root = make_fullscreen(screen);
    /* Bezel ring carrying the 60-minute scale, then the black dial. */
    make_circle(d->root, DIAL_R, lv_color_black(), 24, lv_color_hex(0x2A2A2A));
    build_ticks(d, DIAL_R - 2, 20, 12, 5, 2, COLOR_LUME, COLOR_TICK);
    for (int h = 1; h <= 12; ++h) {
        if (h == 3) continue; /* date window sits here */
        float x1, y1, x2, y2;
        polar(DIAL_R - 120, h * 30.0f, &x1, &y1);
        polar(DIAL_R - 62, h * 30.0f, &x2, &y2);
        static lv_point_precise_t markers[12][2];
        markers[h - 1][0].x = x1; markers[h - 1][0].y = y1;
        markers[h - 1][1].x = x2; markers[h - 1][1].y = y2;
        lv_obj_t *marker = make_line(d->root, h == 12 ? 26 : 18, COLOR_LUME);
        lv_line_set_points(marker, markers[h - 1], 2);
    }
    lv_obj_t *window = make_plain(d->root);
    lv_obj_set_size(window, 84, 50);
    lv_obj_align(window, LV_ALIGN_CENTER, DIAL_R - 130, 0);
    lv_obj_set_style_bg_color(window, lv_color_hex(0xEEEEEE), 0);
    lv_obj_set_style_bg_opa(window, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(window, 4, 0);
    d->date = make_text(window, "1", &lv_font_montserrat_32, lv_color_black());
    lv_obj_center(d->date);
    d->hour = make_line(d->root, 18, COLOR_LUME);
    d->minute = make_line(d->root, 12, COLOR_LUME);
    d->second = make_line(d->root, 3, lv_color_hex(0xFF3B30));
    make_circle(d->root, 12, lv_color_hex(0xFF3B30), 0, lv_color_black());
}

static void update_analog(analog_t *d, const struct tm *t, bool diver)
{
    float sec = (float)t->tm_sec;
    float min = t->tm_min + sec / 60.0f;
    float hour = (t->tm_hour % 12) + min / 60.0f;
    set_hand(d->hour, d->hour_pts, hour * 30.0f, 18, diver ? 125 : 135);
    set_hand(d->minute, d->minute_pts, min * 6.0f, 24, diver ? 190 : 200);
    set_hand(d->second, d->second_pts, sec * 6.0f, 50, diver ? 215 : 230);
    if (diver) {
        char text[4];
        snprintf(text, sizeof(text), "%d", t->tm_mday);
        lv_label_set_text(d->date, text);
    }
}

static void draw_flip(void)
{
    for (int y = 0; y < FLIP_H; ++y) {
        uint16_t *row = s_flip_pixels + y * FLIP_W;
        for (int x = 0; x < FLIP_W; ++x) row[x] = rgb565_gray(flip_face_pixel(&s_flip, s_flip_frame_ms, x, y));
    }
    lv_obj_invalidate(s_flip_canvas);
}

static void fill_square(int x0, int y0, int size, uint16_t color)
{
    for (int y = y0; y < y0 + size; ++y)
        for (int x = x0; x < x0 + size; ++x) s_digital_pixels[y * DIGITAL_W + x] = color;
}

static void draw_digital(const struct tm *t)
{
    memset(s_digital_pixels, 0, DIGITAL_W * DIGITAL_H * sizeof(uint16_t));
    static const int slot_x[4] = {0, 80, 200, 280};
    int digits[4] = {t->tm_hour / 10, t->tm_hour % 10, t->tm_min / 10, t->tm_min % 10};
    for (int i = 0; i < 4; ++i) {
        for (int y = 0; y < DIGITAL_H; ++y) {
            uint16_t *row = s_digital_pixels + y * DIGITAL_W + slot_x[i];
            for (int x = 0; x < 80; ++x) row[x] = rgb565_gray(flip_face_digit_alpha(digits[i], x, y));
        }
    }
    uint16_t white = rgb565_gray(235);
    fill_square(171, 44, 18, white);
    fill_square(171, 90, 18, white);
    lv_obj_invalidate(s_digital_canvas);

    char date[32];
    strftime(date, sizeof(date), "%a, %b %d", t);
    lv_label_set_text(s_digital_date, date);
}

static lv_obj_t *make_scaled_canvas(lv_obj_t *parent, uint16_t *pixels, int w, int h, int y_offset)
{
    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, pixels, w, h, LV_COLOR_FORMAT_RGB565);
    lv_image_set_scale(canvas, SCALE_2X);
    lv_image_set_antialias(canvas, true);
    lv_obj_align(canvas, LV_ALIGN_CENTER, 0, y_offset);
    return canvas;
}

static void refresh_dots(void)
{
    for (int i = 0; i < CLOCK_FACE_COUNT; ++i)
        lv_obj_set_style_bg_color(s_dots[i], i == s_face ? lv_color_white() : lv_color_hex(0x555555), 0);
    lv_obj_remove_flag(s_dots_box, LV_OBJ_FLAG_HIDDEN);
    s_dots_until_ms = app_uptime_ms() + DOTS_VISIBLE_MS;
}

static void show_face(int face)
{
    for (int i = 0; i < CLOCK_FACE_COUNT; ++i) {
        if (i == face) lv_obj_remove_flag(s_roots[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_roots[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_face = face;
    s_key = -1;
    if (face == CLOCK_FACE_FLIP) memset(&s_flip, 0, sizeof(s_flip)); /* no animation from stale digits */
}

bool clock_faces_create(lv_obj_t *screen)
{
    s_flip_pixels = heap_caps_calloc(FLIP_W * FLIP_H, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_digital_pixels = heap_caps_calloc(DIGITAL_W * DIGITAL_H, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_flip_pixels || !s_digital_pixels) {
        ESP_LOGE("faces", "face buffer allocation failed");
        return false;
    }
    s_roots[CLOCK_FACE_FLIP] = make_fullscreen(screen);
    s_flip_canvas = make_scaled_canvas(s_roots[CLOCK_FACE_FLIP], s_flip_pixels, FLIP_W, FLIP_H, 0);

    s_roots[CLOCK_FACE_DIGITAL] = make_fullscreen(screen);
    s_digital_canvas = make_scaled_canvas(s_roots[CLOCK_FACE_DIGITAL], s_digital_pixels, DIGITAL_W, DIGITAL_H, 20);
    s_digital_date = make_text(s_roots[CLOCK_FACE_DIGITAL], "", &lv_font_montserrat_32, lv_color_hex(0x9A9A9A));
    lv_obj_align(s_digital_date, LV_ALIGN_TOP_MID, 0, 80);

    build_classic(screen);
    s_roots[CLOCK_FACE_CLASSIC] = s_classic.root;
    build_diver(screen);
    s_roots[CLOCK_FACE_DIVER] = s_diver.root;

    s_dots_box = make_plain(screen);
    lv_obj_set_size(s_dots_box, 3 * 24 + 10, 10);
    lv_obj_align(s_dots_box, LV_ALIGN_BOTTOM_MID, 0, -14);
    for (int i = 0; i < CLOCK_FACE_COUNT; ++i) {
        s_dots[i] = make_plain(s_dots_box);
        lv_obj_set_size(s_dots[i], 10, 10);
        lv_obj_set_pos(s_dots[i], i * 24, 0);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
    }
    show_face(clock_prefs_face());
    lv_obj_add_flag(s_dots_box, LV_OBJ_FLAG_HIDDEN);
    return true;
}

void clock_faces_update(const struct tm *t, uint64_t frame_ms)
{
    if (s_dots_until_ms && frame_ms > s_dots_until_ms) {
        lv_obj_add_flag(s_dots_box, LV_OBJ_FLAG_HIDDEN);
        s_dots_until_ms = 0;
    }
    switch (s_face) {
    case CLOCK_FACE_FLIP:
        if (flip_face_update(&s_flip, t->tm_hour * 60 + t->tm_min, frame_ms)) {
            s_flip_frame_ms = frame_ms;
            draw_flip();
        }
        break;
    case CLOCK_FACE_DIGITAL: {
        int32_t key = t->tm_yday * 1440 + t->tm_hour * 60 + t->tm_min;
        if (key != s_key) { s_key = key; draw_digital(t); }
        break;
    }
    default: {
        int32_t key = t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
        if (key != s_key) {
            s_key = key;
            update_analog(s_face == CLOCK_FACE_DIVER ? &s_diver : &s_classic, t, s_face == CLOCK_FACE_DIVER);
        }
    }
    }
    s_ready = true;
}

bool clock_faces_ready(void) { return s_ready; }

void clock_faces_select(int face)
{
    if (face < 0 || face >= CLOCK_FACE_COUNT) return;
    if (face != s_face) show_face(face);
    clock_prefs_set_face(face);
    refresh_dots();
}

void clock_faces_step(int delta)
{
    clock_faces_select((s_face + delta + CLOCK_FACE_COUNT) % CLOCK_FACE_COUNT);
}
