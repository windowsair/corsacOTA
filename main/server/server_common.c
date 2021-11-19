#include <stdint.h>
#include "main/ota_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "mdns.h"

static const char *TAG = "server_common";

void mdns_setup()
{
    // initialize mDNS
    int ret;
    ret = mdns_init();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "mDNS initialize failed:%d", ret);
        return;
    }

    // set mDNS hostname
    ret = mdns_hostname_set(CONFIG_MDNS_HOSTNAME);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "mDNS set hostname failed:%d", ret);
        return;
    }
    ESP_LOGI(TAG, "mDNS hostname set to: [%s]", CONFIG_MDNS_HOSTNAME);

    // set default mDNS instance name
    ret = mdns_instance_name_set(CONFIG_MDNS_INSTANCE);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "mDNS set instance name failed:%d", ret);
        return;
    }
    ESP_LOGI(TAG, "mDNS instance name set to: [%s]", CONFIG_MDNS_INSTANCE);
}
