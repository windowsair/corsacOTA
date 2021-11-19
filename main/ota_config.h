#ifndef _OTA_CONFIG_H_
#define _OTA_CONFIG_H_

#define CONFIG_EXAMPLE_WIFI_SSID "OTA"
#define CONFIG_EXAMPLE_WIFI_PASSWORD "12345678"

// Use the address "ota.local" to access the device
#define CONFIG_MDNS_HOSTNAME "ota"
#define CONFIG_MDNS_INSTANCE "corsacOTA mDNS"

#define CONFIG_EXAMPLE_IPV4 1
//#define CONFIG_EXAMPLE_IPV6 1

#define CONFIG_OTA_SERVER_PORT 443


#if (!defined CONFIG_EXAMPLE_IPV4 && !defined CONFIG_EXAMPLE_IPV6)
#error Need to specify a version
#endif

#if (defined CONFIG_EXAMPLE_IPV4 && defined CONFIG_EXAMPLE_IPV6)
#error Only one version can be specified
#endif

#endif