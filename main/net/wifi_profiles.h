#pragma once
#include <stdbool.h>
#define WIFI_PROFILE_LIMIT 16
typedef struct { char ssid[33]; char password[65]; } wifi_profile_t;
typedef struct { unsigned count; wifi_profile_t entries[WIFI_PROFILE_LIMIT]; } wifi_profiles_t;
int wifi_profile_find(const wifi_profiles_t *profiles, const char *ssid);
bool wifi_profile_credentials_valid(const char *ssid, const char *password);
void wifi_profile_remember(wifi_profiles_t *profiles, const char *ssid, const char *password);
