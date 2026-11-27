/* Settings pages (menu, Wi-Fi, password, display, time zone) for the 1024x600 panel.
 * All functions run in the LVGL task; Wi-Fi work happens in wifi_service task. */
#include "settings_ui.h"
#include <stdio.h>
#include <string.h>
#include "app_config.h"
#include "brightness.h"
#include "clock_faces.h"
#include "clock_prefs.h"
#include "wifi_service.h"

#define HEADER_H 88
#define MARGIN 24
#define POLL_MS 200
#define PASSWORD_MIN 8
#define PASSWORD_MAX 64
#define COLOR_BG lv_color_black()
#define COLOR_PANEL lv_color_hex(0x242424)
#define COLOR_LIST lv_color_hex(0x181818)
#define COLOR_TEXT lv_color_hex(0xEEEEEE)
#define COLOR_DIM lv_color_hex(0x8C8C8C)

typedef enum { PAGE_CLOCK, PAGE_SETTINGS, PAGE_WIFI, PAGE_PASSWORD, PAGE_BRIGHTNESS, PAGE_ZONE } page_t; /* PAGE_BRIGHTNESS is the Display page */

static struct {
    lv_obj_t *root, *heading, *menu, *networks, *password, *brightness, *zones, *zone_list;
    lv_obj_t *net_status, *list, *ssid_label, *textarea, *hint, *keyboard;
    lv_obj_t *bright_value, *bright_hint, *slider, *dim_switch, *face_buttons[CLOCK_FACE_COUNT];
    page_t page;
    wifi_service_snapshot_t net;
    char selected_ssid[33];
    unsigned rendered_revision;
} ui;

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void highlight_face(void);
static void highlight_zone(void);

static void show_page(page_t page)
{
    if (ui.page == PAGE_PASSWORD && page != PAGE_PASSWORD) lv_textarea_set_text(ui.textarea, "");
    ui.page = page;
    set_hidden(ui.root, page == PAGE_CLOCK);
    set_hidden(ui.menu, page != PAGE_SETTINGS);
    set_hidden(ui.networks, page != PAGE_WIFI);
    set_hidden(ui.password, page != PAGE_PASSWORD);
    set_hidden(ui.brightness, page != PAGE_BRIGHTNESS);
    set_hidden(ui.zones, page != PAGE_ZONE);
    lv_label_set_text(ui.heading, page == PAGE_BRIGHTNESS ? "Display" : page == PAGE_SETTINGS ? "Settings"
                                   : page == PAGE_PASSWORD ? "Password" : page == PAGE_ZONE ? "Time zone" : "Wi-Fi");
    if (page == PAGE_BRIGHTNESS) {
        char value[12];
        snprintf(value, sizeof(value), "%d%%", brightness_get_percent());
        lv_label_set_text(ui.bright_value, value);
        lv_label_set_text(ui.bright_hint, "Drag to adjust. Saved automatically.");
        lv_slider_set_value(ui.slider, brightness_get_percent(), LV_ANIM_OFF);
        if (clock_prefs_auto_dim()) lv_obj_add_state(ui.dim_switch, LV_STATE_CHECKED);
        else lv_obj_remove_state(ui.dim_switch, LV_STATE_CHECKED);
        highlight_face();
    }
    if (page == PAGE_ZONE) highlight_zone();
    if (page == PAGE_PASSWORD) {
        lv_label_set_text(ui.ssid_label, ui.selected_ssid);
        lv_label_set_text(ui.hint, "8-63 characters, or 64 hex digits");
        lv_textarea_set_text(ui.textarea, "");
    }
}

static void request_scan(void)
{
    show_page(PAGE_WIFI);
    lv_label_set_text(ui.net_status, wifi_service_request_scan() != 0 ? "Wi-Fi busy. Tap Refresh to retry." : "Scanning...");
}

static void network_clicked(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    if (ui.page != PAGE_WIFI || index >= ui.net.count || ui.net.scanning) return;
    const wifi_service_ap_t *ap = &ui.net.access_points[index];
    if (!ap->supported) { lv_label_set_text(ui.net_status, "This security type is not supported."); return; }
    snprintf(ui.selected_ssid, sizeof(ui.selected_ssid), "%s", ap->ssid);
    bool retry = ui.net.state == WIFI_SERVICE_FAILED && !strcmp(ap->ssid, ui.net.ssid);
    if (ap->secured && (!ap->saved || retry)) show_page(PAGE_PASSWORD);
    else if (wifi_service_request_connect(ap->ssid, ap->secured ? NULL : "") != 0)
        lv_label_set_text(ui.net_status, "Wi-Fi busy. Try again.");
    else lv_label_set_text(ui.net_status, "Connecting...");
}

static void render_networks(void)
{
    lv_obj_clean(ui.list);
    for (size_t i = 0; i < ui.net.count; ++i) {
        const wifi_service_ap_t *ap = &ui.net.access_points[i];
        const char *status = !ap->supported ? "Unsupported security"
            : ui.net.state == WIFI_SERVICE_CONNECTED && !strcmp(ap->ssid, ui.net.ssid) ? "Connected"
            : ap->saved ? "Saved" : ap->secured ? "Password required" : "Open";
        char label[112];
        snprintf(label, sizeof(label), "%s\n%s   %d dBm", ap->ssid, status, ap->rssi);
        lv_obj_t *button = lv_list_add_button(ui.list, NULL, label);
        lv_obj_set_style_bg_color(button, COLOR_LIST, 0);
        lv_obj_set_style_text_color(button, COLOR_TEXT, 0);
        lv_obj_set_style_text_font(button, &lv_font_montserrat_24, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_add_event_cb(button, network_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
}

static void poll_network(lv_timer_t *timer)
{
    (void)timer;
    if (ui.page != PAGE_WIFI) return;
    if (wifi_service_read_snapshot(&ui.net) != 0) {
        lv_label_set_text(ui.net_status, "Wi-Fi unavailable. Restart to retry.");
        return;
    }
    if (ui.net.revision == ui.rendered_revision) return;
    ui.rendered_revision = ui.net.revision;
    char message[128];
    if (ui.net.scanning) snprintf(message, sizeof(message), "Scanning...");
    else if (ui.net.state == WIFI_SERVICE_CONNECTED) snprintf(message, sizeof(message), "%s: %s", ui.net.message, ui.net.ssid);
    else snprintf(message, sizeof(message), "%s", ui.net.message);
    if (!ui.net.scanning && !ui.net.count && ui.net.state == WIFI_SERVICE_IDLE)
        snprintf(message, sizeof(message), "No networks found. Tap Refresh.");
    lv_label_set_text(ui.net_status, message);
    render_networks();
}

static void back_clicked(lv_event_t *e)
{
    (void)e;
    if (ui.page == PAGE_BRIGHTNESS && brightness_save() != 0) {
        lv_label_set_text(ui.bright_hint, "Could not save. Tap Back to retry.");
        return;
    }
    show_page(ui.page == PAGE_SETTINGS ? PAGE_CLOCK : ui.page == PAGE_PASSWORD ? PAGE_WIFI : PAGE_SETTINGS);
}

static void open_wifi_clicked(lv_event_t *e) { (void)e; ui.rendered_revision = UINT32_MAX; request_scan(); }
static void highlight_face(void)
{
    for (int i = 0; i < CLOCK_FACE_COUNT; ++i)
        lv_obj_set_style_bg_color(ui.face_buttons[i],
                                  i == clock_prefs_face() ? lv_color_hex(0x2F6FDE) : COLOR_PANEL, 0);
}

static void highlight_zone(void)
{
    for (int i = 0; i < CLOCK_PREFS_ZONE_COUNT; ++i) {
        lv_obj_t *button = lv_obj_get_child(ui.zone_list, i);
        lv_obj_set_style_bg_color(button, i == clock_prefs_zone() ? lv_color_hex(0x2F6FDE) : COLOR_LIST, 0);
    }
}

static void face_clicked(lv_event_t *e)
{
    clock_faces_select((int)(uintptr_t)lv_event_get_user_data(e));
    highlight_face();
}

static void dim_toggled(lv_event_t *e)
{
    clock_prefs_set_auto_dim(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
}

static void zone_clicked(lv_event_t *e)
{
    clock_prefs_set_zone((int)(uintptr_t)lv_event_get_user_data(e));
    highlight_zone();
}

static void open_zone_clicked(lv_event_t *e) { (void)e; show_page(PAGE_ZONE); }
static void open_brightness_clicked(lv_event_t *e) { (void)e; show_page(PAGE_BRIGHTNESS); }

static void slider_changed(lv_event_t *e)
{
    (void)e;
    if (ui.page != PAGE_BRIGHTNESS) return;
    int error = brightness_set_percent((int)lv_slider_get_value(ui.slider));
    char value[12];
    snprintf(value, sizeof(value), "%d%%", brightness_get_percent());
    lv_label_set_text(ui.bright_value, value);
    if (error) {
        lv_label_set_text(ui.bright_hint, "Could not change brightness. Try again.");
        lv_slider_set_value(ui.slider, brightness_get_percent(), LV_ANIM_OFF);
    } else lv_label_set_text(ui.bright_hint, "Drag to adjust. Saved automatically.");
}

static void slider_released(lv_event_t *e)
{
    (void)e;
    if (ui.page == PAGE_BRIGHTNESS && brightness_save() != 0)
        lv_label_set_text(ui.bright_hint, "Could not save. Tap Back to retry.");
}

static void password_ready(lv_event_t *e)
{
    (void)e;
    char password[PASSWORD_MAX + 1];
    snprintf(password, sizeof(password), "%s", lv_textarea_get_text(ui.textarea));
    if (strlen(password) >= PASSWORD_MIN && wifi_service_request_connect(ui.selected_ssid, password) == 0) {
        show_page(PAGE_WIFI);
        lv_label_set_text(ui.net_status, "Connecting...");
    } else lv_label_set_text(ui.hint, "Check password length, then try OK again.");
    memset(password, 0, sizeof(password));
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int w, int h, lv_event_cb_t cb)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_color(button, COLOR_PANEL, 0);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    if (cb) lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = make_label(button, text, &lv_font_montserrat_24, COLOR_TEXT);
    lv_obj_center(label);
    return label ? button : NULL;
}

static lv_obj_t *make_page(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, APP_SCREEN_W, APP_SCREEN_H - HEADER_H);
    lv_obj_set_pos(page, 0, HEADER_H);
    return page;
}

void settings_ui_create(lv_obj_t *screen)
{
    ui.rendered_revision = UINT32_MAX;
    ui.root = lv_obj_create(screen);
    lv_obj_remove_style_all(ui.root);
    lv_obj_set_size(ui.root, APP_SCREEN_W, APP_SCREEN_H);
    lv_obj_set_style_bg_color(ui.root, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(ui.root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(ui.root, LV_OBJ_FLAG_SCROLLABLE);
    set_hidden(ui.root, true);

    lv_obj_t *back = make_button(ui.root, "Back", 130, 60, back_clicked);
    lv_obj_set_pos(back, MARGIN, 14);
    ui.heading = make_label(ui.root, "Settings", &lv_font_montserrat_32, COLOR_TEXT);
    lv_obj_set_pos(ui.heading, 190, 24);

    ui.menu = make_page(ui.root);
    lv_obj_t *wifi = make_button(ui.menu, "Wi-Fi     >", APP_SCREEN_W - 2 * MARGIN, 90, open_wifi_clicked);
    lv_obj_set_pos(wifi, MARGIN, MARGIN);
    lv_obj_t *bright = make_button(ui.menu, "Display     >", APP_SCREEN_W - 2 * MARGIN, 90, open_brightness_clicked);
    lv_obj_set_pos(bright, MARGIN, MARGIN + 110);
    lv_obj_t *zone = make_button(ui.menu, "Time zone     >", APP_SCREEN_W - 2 * MARGIN, 90, open_zone_clicked);
    lv_obj_set_pos(zone, MARGIN, MARGIN + 220);

    ui.networks = make_page(ui.root);
    ui.net_status = make_label(ui.networks, "Scanning...", &lv_font_montserrat_24, COLOR_TEXT);
    lv_obj_set_width(ui.net_status, 700);
    lv_obj_set_pos(ui.net_status, MARGIN, 12);
    lv_obj_t *refresh = make_button(ui.networks, "Refresh", 170, 56, open_wifi_clicked);
    lv_obj_set_pos(refresh, APP_SCREEN_W - MARGIN - 170, 4);
    ui.list = lv_list_create(ui.networks);
    lv_obj_set_size(ui.list, APP_SCREEN_W - 2 * MARGIN, APP_SCREEN_H - HEADER_H - 90);
    lv_obj_set_pos(ui.list, MARGIN, 76);
    lv_obj_set_style_bg_color(ui.list, COLOR_LIST, 0);
    lv_obj_set_style_border_width(ui.list, 0, 0);
    lv_obj_set_style_radius(ui.list, 12, 0);

    ui.password = make_page(ui.root);
    ui.ssid_label = make_label(ui.password, "", &lv_font_montserrat_28, COLOR_TEXT);
    lv_obj_set_pos(ui.ssid_label, MARGIN, 0);
    ui.textarea = lv_textarea_create(ui.password);
    lv_textarea_set_one_line(ui.textarea, true);
    lv_textarea_set_password_mode(ui.textarea, true);
    lv_textarea_set_max_length(ui.textarea, PASSWORD_MAX);
    lv_textarea_set_placeholder_text(ui.textarea, "Enter password");
    lv_obj_set_size(ui.textarea, APP_SCREEN_W - 2 * MARGIN, 56);
    lv_obj_set_pos(ui.textarea, MARGIN, 40);
    lv_obj_set_style_text_font(ui.textarea, &lv_font_montserrat_24, 0);
    ui.hint = make_label(ui.password, "", &lv_font_montserrat_20, COLOR_DIM);
    lv_obj_set_pos(ui.hint, MARGIN, 104);
    ui.keyboard = lv_keyboard_create(ui.password);
    lv_keyboard_set_textarea(ui.keyboard, ui.textarea);
    lv_obj_set_size(ui.keyboard, APP_SCREEN_W, 300);
    lv_obj_align(ui.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(ui.keyboard, password_ready, LV_EVENT_READY, NULL);

    ui.brightness = make_page(ui.root);
    ui.bright_value = make_label(ui.brightness, "", &lv_font_montserrat_32, COLOR_TEXT);
    lv_obj_align(ui.bright_value, LV_ALIGN_TOP_MID, 0, 40);
    ui.slider = lv_slider_create(ui.brightness);
    lv_slider_set_range(ui.slider, 5, 100);
    lv_obj_set_size(ui.slider, 700, 28);
    lv_obj_align(ui.slider, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_add_event_cb(ui.slider, slider_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui.slider, slider_released, LV_EVENT_RELEASED, NULL);
    ui.bright_hint = make_label(ui.brightness, "", &lv_font_montserrat_20, COLOR_DIM);
    lv_obj_align(ui.bright_hint, LV_ALIGN_TOP_MID, 0, 195);

    lv_obj_t *dim_label = make_label(ui.brightness, "Night auto-dim (22:00-07:00, max 10%)",
                                     &lv_font_montserrat_24, COLOR_TEXT);
    lv_obj_set_pos(dim_label, MARGIN + 40, 262);
    ui.dim_switch = lv_switch_create(ui.brightness);
    lv_obj_set_pos(ui.dim_switch, APP_SCREEN_W - MARGIN - 40 - 80, 252);
    lv_obj_add_event_cb(ui.dim_switch, dim_toggled, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *face_label = make_label(ui.brightness, "Clock face (or swipe left/right on the clock)",
                                      &lv_font_montserrat_24, COLOR_TEXT);
    lv_obj_set_pos(face_label, MARGIN + 40, 340);
    static const char *const face_names[CLOCK_FACE_COUNT] = {"Flip", "Digital", "Classic", "Diver"};
    for (int i = 0; i < CLOCK_FACE_COUNT; ++i) {
        ui.face_buttons[i] = make_button(ui.brightness, face_names[i], 200, 64, NULL);
        lv_obj_add_event_cb(ui.face_buttons[i], face_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_set_pos(ui.face_buttons[i], MARGIN + 40 + i * 220, 388);
    }

    ui.zones = make_page(ui.root);
    ui.zone_list = lv_list_create(ui.zones);
    lv_obj_set_size(ui.zone_list, APP_SCREEN_W - 2 * MARGIN, APP_SCREEN_H - HEADER_H - 20);
    lv_obj_set_pos(ui.zone_list, MARGIN, 0);
    lv_obj_set_style_bg_color(ui.zone_list, COLOR_LIST, 0);
    lv_obj_set_style_border_width(ui.zone_list, 0, 0);
    lv_obj_set_style_radius(ui.zone_list, 12, 0);
    for (int i = 0; i < CLOCK_PREFS_ZONE_COUNT; ++i) {
        lv_obj_t *button = lv_list_add_button(ui.zone_list, NULL, clock_prefs_zones[i].name);
        lv_obj_set_style_text_color(button, COLOR_TEXT, 0);
        lv_obj_set_style_text_font(button, &lv_font_montserrat_24, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_add_event_cb(button, zone_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }

    lv_timer_create(poll_network, POLL_MS, NULL);
    show_page(PAGE_CLOCK);
}

void settings_ui_open(void)
{
    if (ui.page == PAGE_CLOCK) show_page(PAGE_SETTINGS);
}

bool settings_ui_is_open(void)
{
    return ui.page != PAGE_CLOCK;
}
