#include <string.h>
#include <sys/param.h>

#include "main/connect/connect.h"
#include "main/ota_config.h"

#include "../../src/corsacOTA.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "mdns.h"


static const char *MDNS_TAG = "server_common";

void mdns_setup() {
    // initialize mDNS
    int ret;
    ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGW(MDNS_TAG, "mDNS initialize failed:%d", ret);
        return;
    }

    // set mDNS hostname
    ret = mdns_hostname_set(CONFIG_MDNS_HOSTNAME);
    if (ret != ESP_OK) {
        ESP_LOGW(MDNS_TAG, "mDNS set hostname failed:%d", ret);
        return;
    }
    ESP_LOGI(MDNS_TAG, "mDNS hostname set to: [%s]", CONFIG_MDNS_HOSTNAME);

    // set default mDNS instance name
    ret = mdns_instance_name_set(CONFIG_MDNS_INSTANCE);
    if (ret != ESP_OK) {
        ESP_LOGW(MDNS_TAG, "mDNS set instance name failed:%d", ret);
        return;
    }
    ESP_LOGI(MDNS_TAG, "mDNS instance name set to: [%s]", CONFIG_MDNS_INSTANCE);
}

void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());

    mdns_setup();
    co_handle_t handle;
    co_config_t config = {
        .thread_name = "corsacOTA",
        .stack_size = 4096,
        .thread_prio = 8,
        .listen_port = 3241,
        .max_listen_num = 4,
        .wait_timeout_sec = 60,
        .wait_timeout_usec = 0,
    };

    corsacOTA_init(&handle, &config);
}