/**
 * @file tcp_server.c
 * @brief Handle main tcp tasks
 * @version 0.1
 * @date 2020-01-22
 *
 * @copyright Copyright (c) 2020
 *
 */
#include <string.h>
#include <stdint.h>
#include <sys/param.h>

#include "main/server/server_common.h"
#include "main/ota_config.h"


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

int kOTASock = -1;

static const char *TAG = "TCP";


void tcp_server_task()
{
    uint8_t tcp_rx_buffer[1500];
    char addr_str[128];
    int addr_family;
    int ip_protocol;
	int on = 1;

    while (1)
    {

#ifdef CONFIG_EXAMPLE_IPV4
        struct sockaddr_in destAddr;
        destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(CONFIG_OTA_SERVER_PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);
#else // IPV6
        struct sockaddr_in6 destAddr;
        bzero(&destAddr.sin6_addr.un, sizeof(destAddr.sin6_addr.un));
        destAddr.sin6_family = AF_INET6;
        destAddr.sin6_port = htons(CONFIG_OTA_SERVER_PORT);
        addr_family = AF_INET6;
        ip_protocol = IPPROTO_IPV6;
        inet6_ntoa_r(destAddr.sin6_addr, addr_str, sizeof(addr_str) - 1);
#endif // CONFIG_EXAMPLE_IPV4

        int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (listen_sock < 0)
        {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

        setsockopt(listen_sock, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof(on));
        setsockopt(listen_sock, IPPROTO_TCP, TCP_NODELAY, (void *)&on, sizeof(on));

        int err = bind(listen_sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
        if (err != 0)
        {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket binded");

        err = listen(listen_sock, 1);
        if (err != 0)
        {
            ESP_LOGE(TAG, "Error occured during listen: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket listening");

#ifdef CONFIG_EXAMPLE_IPV6
        struct sockaddr_in6 sourceAddr; // Large enough for both IPv4 or IPv6
#else
        struct sockaddr_in sourceAddr;
#endif
        uint32_t addrLen = sizeof(sourceAddr);
        while (1)
        {
            kOTASock = accept(listen_sock, (struct sockaddr *)&sourceAddr, &addrLen);
            if (kOTASock < 0)
            {
                ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
                break;
            }
			ESP_LOGI(TAG, "Socket accepted");

            while (1)
            {
                int len = recv(kOTASock, tcp_rx_buffer, sizeof(tcp_rx_buffer), 0);
                // Error occured during receiving
                if (len < 0)
                {
                    ESP_LOGE(TAG, "recv failed: errno %d", errno);
                    break;
                }
                // Connection closed
                else if (len == 0)
                {
                    ESP_LOGI(TAG, "Connection closed");
                    break;
                }
                // Data received
                else
                {
#ifdef CONFIG_EXAMPLE_IPV6
                // Get the sender's ip address as string
                    if (sourceAddr.sin6_family == PF_INET)
                    {
                        inet_ntoa_r(((struct sockaddr_in *)&sourceAddr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                    }
                    else if (sourceAddr.sin6_family == PF_INET6)
                    {
                        inet6_ntoa_r(sourceAddr.sin6_addr, addr_str, sizeof(addr_str) - 1);
                    }
#else
                //inet_ntoa_r(((struct sockaddr_in *)&sourceAddr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
#endif // CONFIG_EXAMPLE_IPV6

                    tcp_data_in_handle(tcp_rx_buffer, len);
                }
            }
            if (kOTASock != -1)
            {
                ESP_LOGE(TAG, "Shutting down socket and restarting...");
                close(kOTASock);
            }
        }
    }
    vTaskDelete(NULL);
}