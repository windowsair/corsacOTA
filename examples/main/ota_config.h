#ifndef _OTA_CONFIG_H_
#define _OTA_CONFIG_H_

#include "sdkconfig.h"
#if (defined CONFIG_IDF_TARGET_ESP8266) && (CONFIG_IDF_TARGET_ESP8266 == 1)
    #define CONFIG_EXAMPLE_WIFI_SSID "OTA"
    #define CONFIG_EXAMPLE_WIFI_PASSWORD "12345678"
#endif

// Use the address "ota.local" to access the device
#define CONFIG_MDNS_HOSTNAME "ota"
#define CONFIG_MDNS_INSTANCE "corsacOTA mDNS"

#endif

