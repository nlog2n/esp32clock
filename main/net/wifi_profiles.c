#include "wifi_profiles.h"
#include <string.h>
#include <ctype.h>
int wifi_profile_find(const wifi_profiles_t *profiles, const char *ssid)
{
    for (unsigned i=0; i<profiles->count; ++i)
        if (!strcmp(profiles->entries[i].ssid,ssid)) return (int)i;
    return -1;
}
bool wifi_profile_credentials_valid(const char *ssid, const char *password)
{
    if (!ssid || !password || !ssid[0] || strlen(ssid)>32) return false;
    size_t length=strlen(password);
    if (!length || (length>=8 && length<=63)) return true;
    if (length!=64) return false;
    for (size_t i=0;i<length;++i) if (!isxdigit((unsigned char)password[i])) return false;
    return true;
}
void wifi_profile_remember(wifi_profiles_t *profiles, const char *ssid, const char *password)
{
    int existing=wifi_profile_find(profiles,ssid);
    unsigned move=existing>=0 ? (unsigned)existing : profiles->count;
    if (move>=WIFI_PROFILE_LIMIT) move=WIFI_PROFILE_LIMIT-1;
    memmove(&profiles->entries[1],&profiles->entries[0],move*sizeof(profiles->entries[0]));
    if (existing<0 && profiles->count<WIFI_PROFILE_LIMIT) ++profiles->count;
    memset(&profiles->entries[0],0,sizeof(profiles->entries[0]));
    strcpy(profiles->entries[0].ssid,ssid);
    strcpy(profiles->entries[0].password,password);
}
