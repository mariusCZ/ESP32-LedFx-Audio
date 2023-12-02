/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 https://github.com/espressif/esp-idf/issues/5381
 */

// Increase allocation needed possibly.

#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_sleep.h"

#include "driver/ledc.h"
#include "mcp3202.h"
#include "network.h"
#include "udpclient.h"

#include "cJSON.h"
#include "mbedtls/base64.h"

MCP_t dev;

#define DEFAULT_VACTROL_VAL 79
#define AGC_INTERVAL 50
#define LEDC_GPIO 4

#define SAMPLE_RATE 30000
#define N_SAMPLES 500

static ledc_channel_config_t ledc_channel;

static void init_hw(void)
{
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_16_BIT,
        .freq_hz = 1220,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    ledc_timer_config(&ledc_timer);
    ledc_channel.channel = LEDC_CHANNEL_0;
    ledc_channel.duty = 0;
    ledc_channel.gpio_num = LEDC_GPIO;
    ledc_channel.speed_mode = LEDC_HIGH_SPEED_MODE;
    ledc_channel.hpoint = 0;
    ledc_channel.timer_sel = LEDC_TIMER_0;
    ledc_channel_config(&ledc_channel);
}

static void init_ledfx(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();
    char *json = NULL;
    cJSON_AddItemToObject(root, "data", data);
    cJSON_AddNumberToObject(root, "id", 1);
    cJSON_AddStringToObject(root, "client", "ESP32");
    cJSON_AddStringToObject(root, "type", "audio_stream_start");
    json = cJSON_Print(root);
    send_ws(json, 0);

    vTaskDelay(10);

    root = cJSON_CreateObject();
    data = cJSON_CreateObject();
    json = NULL;
    cJSON_AddNumberToObject(data, "sampleRate", SAMPLE_RATE);
    cJSON_AddNumberToObject(data, "bufferSize", N_SAMPLES);
    cJSON_AddNumberToObject(data, "bits", 12);
    cJSON_AddItemToObject(root, "data", data);
    cJSON_AddNumberToObject(root, "id", 1);
    cJSON_AddStringToObject(root, "client", "ESP32");
    cJSON_AddStringToObject(root, "type", "audio_stream_config");
    json = cJSON_Print(root);
    send_ws(json, 0);

    cJSON_Delete(root);
}

static void send_ledfx_data(uint16_t samps[])
{
    cJSON *root = cJSON_CreateObject();
    char *json = NULL;

    unsigned char inBuf[N_SAMPLES*2];
    unsigned char outBuf[N_SAMPLES*3];
    size_t outlen = N_SAMPLES*3;

    for (int i = 0; i < N_SAMPLES; i++) {
        memcpy(inBuf+i*2, (char*)&samps[i], 2);
    }
    mbedtls_base64_encode(outBuf, N_SAMPLES*3, &outlen, inBuf, N_SAMPLES*2);

    cJSON_AddStringToObject(root, "data", (const char*)outBuf);
    cJSON_AddNumberToObject(root, "id", 1);
    cJSON_AddStringToObject(root, "client", "ESP32");
    cJSON_AddStringToObject(root, "type", "afast");
    json = cJSON_Print(root);
    send_ws(json, 0);
    //printf("%s\n%s\n", outBuf, json);

    cJSON_Delete(root);
}

static void send_ledfx_data_udp(uint16_t samps[])
{
    char buf[N_SAMPLES*2];

    for (int i = 0; i < N_SAMPLES; i++) {
        memcpy(buf+i*2, (char*)&samps[i], 2);
        //memcpy(buf+i*2, (char*)&sineLookupTable[i], 2);
    }
    send_udp(buf, N_SAMPLES*2);
    //send_udp(buf, 10);
}

void main_thread() {
    unsigned int vactrol_val = DEFAULT_VACTROL_VAL;
    init_hw();
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, DEFAULT_VACTROL_VAL);
    while(1) {
        uint16_t samples[N_SAMPLES] = {0};
        if (mcpReadData(&dev, 0, samples, N_SAMPLES)) {
            vactrol_val += 1;
            //ESP_LOGI("AG", "peak %d", vactrol_val);
            ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, vactrol_val);
            ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
        }
        send_ledfx_data_udp(samples);
        vTaskDelay(1);
    }
}

void app_main(void)
{
    int udpPort = 0;
    mcpInit(&dev, MCP_SINGLE);
    init_wifi();

    xTaskCreatePinnedToCore(websocket_app_start,
        "websocket",
        2196,
        NULL,
        1,
        NULL,
        1);
    
    vTaskDelay(20);
    while (!wait_for_ws());
    vTaskDelay(20);
    init_ledfx();

    while (udpPort < 1) {
        udpPort = msg_check();
        vTaskDelay(100);
    }
    check_connection(); // Clears the disconnect sema.

    xTaskCreatePinnedToCore(main_thread,
        "main_thread",
        4096+4096,
        NULL,
        1,
        NULL,
        0);

    xTaskCreatePinnedToCore(udp_client_task,
        "udp",
        2048,
        (void *)udpPort,
        19,
        NULL,
        1);
    
    while (1) {
        vTaskDelay(2000);
        if (check_connection()) {
            shutdown_socket();
            while (!wait_for_ws());
            vTaskDelay(20);
            init_ledfx();

            udpPort = 0;
            while (udpPort < 1) {
                udpPort = msg_check();
                vTaskDelay(100);
            }
            check_connection(); // Clears the sema.

            xTaskCreatePinnedToCore(udp_client_task,
                "udp",
                2048,
                (void *)udpPort,
                19,
                NULL,
                1);
        }
    }
}

