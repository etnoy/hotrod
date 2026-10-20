#pragma once
#if __has_include("local_config.h")
#include "local_config.h"
#endif
#ifndef GARO_WIFI_SSID
#define GARO_WIFI_SSID ""
#endif
#ifndef GARO_WIFI_PASSWORD
#define GARO_WIFI_PASSWORD ""
#endif
#ifndef GARO_API_TOKEN
#define GARO_API_TOKEN ""
#endif
#ifndef GARO_HOSTNAME
#define GARO_HOSTNAME "garo-idvn"
#endif
#ifndef GARO_IDLE_GPIO4
#define GARO_IDLE_GPIO4 1
#endif
#ifndef GARO_IDLE_GPIO5
#define GARO_IDLE_GPIO5 0
#endif
static_assert(sizeof(GARO_WIFI_SSID) <= 33, "SSID must be at most 32 bytes");
static_assert(sizeof(GARO_WIFI_PASSWORD) <= 64, "Password must be at most 63 bytes");
static_assert(sizeof(GARO_WIFI_PASSWORD) == 1 || sizeof(GARO_WIFI_PASSWORD) >= 9,
              "WPA password must be empty or at least 8 bytes");
static_assert(sizeof(GARO_API_TOKEN) == 1 || sizeof(GARO_API_TOKEN) >= 25,
              "API token must be at least 24 bytes");
static_assert(sizeof(GARO_API_TOKEN) <= 129, "API token must be at most 128 bytes");
static_assert(sizeof(GARO_HOSTNAME) >= 2 && sizeof(GARO_HOSTNAME) <= 33,
              "Hostname must be 1 to 32 bytes");
static_assert((GARO_IDLE_GPIO4 == 0 || GARO_IDLE_GPIO4 == 1) &&
              (GARO_IDLE_GPIO5 == 0 || GARO_IDLE_GPIO5 == 1), "GPIO idle levels must be zero or one");
