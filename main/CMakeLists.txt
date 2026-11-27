idf_component_register(
    SRCS
        "main.c"
        "faces/clock_faces.c"
        "faces/flip_face.c"
        "net/wifi_profiles.c"
        "net/wifi_service.c"
        "settings/brightness.c"
        "settings/clock_prefs.c"
        "settings/settings_ui.c"
    INCLUDE_DIRS "common" "faces" "net" "settings"
    PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_timer esp_lcd)
