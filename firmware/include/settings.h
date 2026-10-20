#pragma once
#if __has_include("local_config.h")
#include "local_config.h"
#endif
#ifndef IDVN_WIFI_SSID
#define IDVN_WIFI_SSID ""
#endif
#ifndef IDVN_WIFI_PASSWORD
#define IDVN_WIFI_PASSWORD ""
#endif
#ifndef IDVN_API_TOKEN
#define IDVN_API_TOKEN ""
#endif
#ifndef IDVN_HOSTNAME
#define IDVN_HOSTNAME "idvn"
#endif
#ifndef IDVN_IDLE_GPIO4
#define IDVN_IDLE_GPIO4 1
#endif
#ifndef IDVN_IDLE_GPIO5
#define IDVN_IDLE_GPIO5 0
#endif
#ifndef IDVN_MQTT_IP
#define IDVN_MQTT_IP ""
#endif
#ifndef IDVN_MQTT_PORT
#define IDVN_MQTT_PORT 1883
#endif
#ifndef IDVN_MQTT_USER
#define IDVN_MQTT_USER ""
#endif
#ifndef IDVN_MQTT_PASSWORD
#define IDVN_MQTT_PASSWORD ""
#endif
#ifndef IDVN_MQTT_PREFIX
#define IDVN_MQTT_PREFIX "hotrod/idvn"
#endif
static_assert(IDVN_MQTT_PORT > 0 && IDVN_MQTT_PORT <= 65535, "Invalid MQTT port");
static_assert(sizeof(IDVN_MQTT_IP) <= 16, "MQTT broker must be a numeric IPv4 address");
static_assert(sizeof(IDVN_MQTT_USER) <= 65 && sizeof(IDVN_MQTT_PASSWORD) <= 129,
              "MQTT username/password too long");
static_assert(sizeof(IDVN_MQTT_USER) > 1 || sizeof(IDVN_MQTT_PASSWORD) == 1,
              "MQTT password requires username");
static_assert(sizeof(IDVN_MQTT_PREFIX) > 1 && sizeof(IDVN_MQTT_PREFIX) <= 97,
              "MQTT prefix must be 1 to 96 bytes");
static_assert(sizeof(IDVN_WIFI_SSID) <= 33, "SSID must be at most 32 bytes");
static_assert(sizeof(IDVN_WIFI_PASSWORD) <= 64, "Password must be at most 63 bytes");
static_assert(sizeof(IDVN_WIFI_PASSWORD) == 1 || sizeof(IDVN_WIFI_PASSWORD) >= 9,
              "WPA password must be empty or at least 8 bytes");
static_assert(sizeof(IDVN_API_TOKEN) == 1 || sizeof(IDVN_API_TOKEN) >= 25,
              "API token must be at least 24 bytes");
static_assert(sizeof(IDVN_API_TOKEN) <= 129, "API token must be at most 128 bytes");
static_assert(sizeof(IDVN_HOSTNAME) >= 2 && sizeof(IDVN_HOSTNAME) <= 33,
              "Hostname must be 1 to 32 bytes");
static_assert((IDVN_IDLE_GPIO4 == 0 || IDVN_IDLE_GPIO4 == 1) &&
              (IDVN_IDLE_GPIO5 == 0 || IDVN_IDLE_GPIO5 == 1), "GPIO idle levels must be zero or one");
