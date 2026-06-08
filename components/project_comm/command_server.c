#include "command_server.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"

#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "control_task.h"
#include "wifi_secrets.h"

/*
 * command_server.c
 *
 * Minimal BSD socket command parser for Day 3-4.
 * This is intentionally short and practical.
 */

#define COMMAND_PORT        5005
#define RX_BUF_SIZE         128
#define WIFI_CONNECTED_BIT  BIT0

static const char *TAG = "command_server";
static EventGroupHandle_t s_wifi_events;

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    (void)ctx;

    switch (event->event_id)
    {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;

        case SYSTEM_EVENT_STA_GOT_IP:
            ESP_LOGI(TAG,
                     "Wi-Fi connected, IP=" IPSTR,
                     IP2STR(&event->event_info.got_ip.ip_info.ip));
            xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
            break;

        case SYSTEM_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "Wi-Fi disconnected; reconnecting");
            xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
            esp_wifi_connect();
            break;

        default:
            break;
    }

    return ESP_OK;
}

static void wifi_start(void)
{
    s_wifi_events = xEventGroupCreate();

    nvs_flash_init();
    tcpip_adapter_init();
    esp_event_loop_init(wifi_event_handler, NULL);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_cfg;
    memset(&wifi_cfg, 0, sizeof(wifi_cfg));

    strncpy((char *)wifi_cfg.sta.ssid,
            WIFI_SSID,
            sizeof(wifi_cfg.sta.ssid) - 1);

    strncpy((char *)wifi_cfg.sta.password,
            WIFI_PASSWORD,
            sizeof(wifi_cfg.sta.password) - 1);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg);
    esp_wifi_start();

    ESP_LOGI(TAG, "connecting to Wi-Fi SSID=%s", WIFI_SSID);

    xEventGroupWaitBits(s_wifi_events,
                        WIFI_CONNECTED_BIT,
                        pdFALSE,
                        pdTRUE,
                        pdMS_TO_TICKS(20000));
}

static void trim(char *s)
{
    size_t n = strlen(s);

    while ((n > 0) &&
           ((s[n - 1] == '\n') ||
            (s[n - 1] == '\r') ||
            (s[n - 1] == ' ') ||
            (s[n - 1] == '\t')))
    {
        s[n - 1] = '\0';
        n--;
    }
}

static void handle_command(const char *cmd, char *resp, size_t resp_len)
{
    if (strcmp(cmd, "ARM") == 0)
    {
        control_arm();
        snprintf(resp, resp_len, "OK ARM\n");
    }
    else if (strcmp(cmd, "DISARM") == 0)
    {
        control_disarm();
        snprintf(resp, resp_len, "OK DISARM\n");
    }
    else if (strcmp(cmd, "STOP") == 0)
    {
        control_stop();
        snprintf(resp, resp_len, "OK STOP\n");
    }
    else if (strcmp(cmd, "STEP_TEST") == 0)
    {
        control_start_step_test();
        snprintf(resp, resp_len, "OK STEP_TEST 500 1500\n");
    }
    else if (strncmp(cmd, "SET_SPEED ", 10) == 0)
    {
        char *end = NULL;
        long rpm = strtol(cmd + 10, &end, 10);

        if ((end == cmd + 10) || (*end != '\0'))
        {
            snprintf(resp, resp_len, "ERR BAD_ARGUMENT\n");
            return;
        }

        int rc = control_set_speed((int32_t)rpm);

        if (rc == 0)
        {
            snprintf(resp, resp_len, "OK SET_SPEED %ld\n", rpm);
        }
        else if (rc == -1)
        {
            snprintf(resp, resp_len, "ERR NOT_ARMED\n");
        }
        else
        {
            snprintf(resp, resp_len, "ERR RPM_RANGE\n");
        }
    }
    else if (strcmp(cmd, "STATUS") == 0)
    {
        control_status_t st = control_get_status();

        snprintf(resp,
                 resp_len,
                 "OK STATUS state=%s cmd=%ld target=%ld actual=%ld duty=%ld error=%ld delta=%ld missed=%lu step=%d\n",
                 control_state_name(st.state),
                 (long)st.cmd,
                 (long)st.target,
                 (long)st.actual,
                 (long)st.duty,
                 (long)st.error,
                 (long)st.delta,
                 (unsigned long)st.missed,
                 st.step_active);
    }
    else
    {
        snprintf(resp, resp_len, "ERR UNKNOWN_COMMAND\n");
    }
}

static void server_task(void *arg)
{
    (void)arg;

    wifi_start();

    int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_fd < 0)
    {
        ESP_LOGE(TAG, "socket failed errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }

    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(COMMAND_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        ESP_LOGE(TAG, "bind failed errno=%d", errno);
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_fd, 4) < 0)
    {
        ESP_LOGE(TAG, "listen failed errno=%d", errno);
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "TCP command server listening on port %d", COMMAND_PORT);

    while (1)
    {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0)
        {
            continue;
        }

        char rx[RX_BUF_SIZE];
        memset(rx, 0, sizeof(rx));

        int len = recv(client_fd, rx, sizeof(rx) - 1, 0);
        if (len > 0)
        {
            rx[len] = '\0';
            trim(rx);

            ESP_LOGI(TAG, "RX: %s", rx);

            char resp[192];
            handle_command(rx, resp, sizeof(resp));

            send(client_fd, resp, strlen(resp), 0);
        }

        close(client_fd);
    }
}

void command_server_start(void)
{
    xTaskCreate(server_task, "cmd_server", 4096, NULL, 4, NULL);
}