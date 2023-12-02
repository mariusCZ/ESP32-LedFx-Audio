#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#define HOST_IP_ADDR "192.168.179.11"

static SemaphoreHandle_t shutdown_sema;
static SemaphoreHandle_t send_sema;

static const char *TAG = "UDP";

static char send_data[2000] = {0};
static int len_data = 0;

void send_udp(char *dat, int len) {
    len_data = len;
    //strncpy(send_data, dat, len);
    for (int i = 0; i < len; i++)
        send_data[i] = dat[i];
    xSemaphoreGive(send_sema);
}

void shutdown_socket()
{
    xSemaphoreGive(shutdown_sema);
}

void udp_client_task(void *pvParameters)
{
    int addr_family = 0;
    int ip_protocol = 0;
    int udpPort = (int *)pvParameters;

    shutdown_sema = xSemaphoreCreateBinary();
    send_sema = xSemaphoreCreateBinary();

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(HOST_IP_ADDR);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(udpPort);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;

    int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    // Set timeout
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

    ESP_LOGI(TAG, "Socket created, sending to %s:%d", HOST_IP_ADDR, udpPort);

    while (1) {
        if (xSemaphoreTake(send_sema, 100) == pdTRUE) {
            //ESP_LOGI(TAG, "Sending WS data");
            int err = sendto(sock, send_data, len_data, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (err < 0) {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                break;
            }
        }

        if (xSemaphoreTake(shutdown_sema, 0) == pdTRUE) break;

        // struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        // socklen_t socklen = sizeof(source_addr);
        // int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

        // // Error occurred during receiving
        // if (len < 0) {
        //     ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
        //     break;
        // }
        // // Data received
        // else {
        //     rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
        //     ESP_LOGI(TAG, "Received %d bytes from %s:", len, host_ip);
        //     ESP_LOGI(TAG, "%s", rx_buffer);
        //     if (strncmp(rx_buffer, "OK: ", 4) == 0) {
        //         ESP_LOGI(TAG, "Received expected message, reconnecting");
        //         break;
        //     }
        // }

    }

    ESP_LOGI(TAG, "Shutting down socket...");
    shutdown(sock, 0);
    close(sock);
    vTaskDelete(NULL);
}