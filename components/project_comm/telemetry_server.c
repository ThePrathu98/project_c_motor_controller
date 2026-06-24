#include "telemetry_server.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "control_task.h"

/*
 * Binary telemetry stream on TCP port 5006.
 *
 * The GUI receives fixed 20-byte little-endian frames at 25 Hz. A sequence
 * number is included so the host can detect packet gaps after reconnects.
 *
 * Frame format used by Python: <IhhhhBBHI
 *   seq, target_rpm, actual_rpm, duty_permille, current_mA,
 *   state, fault_flags, missed_low16, uptime_ms
 */

#define TELEMETRY_PORT          5006

#define TELEMETRY_PERIOD_MS     40

#define TELEMETRY_FRAME_LEN     20

static const char *TAG = "telemetry_server";

static void put_u16_le(uint8_t *buf, uint16_t v)
{
    buf[0] = (uint8_t)(v & 0xFFU);
    buf[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static void put_i16_le(uint8_t *buf, int16_t v)
{
    /* Preserve the two's-complement bit pattern when packing signed values. */
    put_u16_le(buf, (uint16_t)v);
}

static void put_u32_le(uint8_t *buf, uint32_t v)
{
    buf[0] = (uint8_t)(v & 0xFFU);
    buf[1] = (uint8_t)((v >> 8) & 0xFFU);
    buf[2] = (uint8_t)((v >> 16) & 0xFFU);
    buf[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static int16_t clamp_i16(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static void build_frame(uint8_t frame[TELEMETRY_FRAME_LEN], uint32_t seq)
{
    /* Observer only: telemetry reads a snapshot and never commands the motor. */
    control_status_t st = control_get_status();

    memset(frame, 0, TELEMETRY_FRAME_LEN);

    put_u32_le(&frame[0], seq);
    put_i16_le(&frame[4],  clamp_i16(st.target));
    put_i16_le(&frame[6],  clamp_i16(st.actual));
    put_i16_le(&frame[8],  clamp_i16(st.duty * 10)); /* percent -> permille */
    put_i16_le(&frame[10], clamp_i16(st.current_ma));
    frame[12] = (uint8_t)st.state;
    frame[13] = st.fault_flags;
    put_u16_le(&frame[14], (uint16_t)(st.missed & 0xFFFFU));
    put_u32_le(&frame[16], st.ticks);
}

static void telemetry_task(void *arg)
{
    (void)arg;

    int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_fd < 0)
    {
        ESP_LOGE(TAG, "socket failed errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }

    /*
     * SO_REUSEADDR helps during repeated manual reconnect tests. It avoids
     * being blocked by a recently closed TCP socket in TIME_WAIT.
     */
    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TELEMETRY_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        ESP_LOGE(TAG, "bind failed errno=%d", errno);
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_fd, 1) < 0)
    {
        ESP_LOGE(TAG, "listen failed errno=%d", errno);
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "binary telemetry listening on port %d", TELEMETRY_PORT);

    uint32_t seq = 0;
    uint8_t frame[TELEMETRY_FRAME_LEN];

    while (1)
    {
        /* One client at a time; a failed send drops back to accept(). */
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0)
        {
            continue;
        }

        ESP_LOGI(TAG, "telemetry client connected");

        TickType_t last_wake = xTaskGetTickCount();

        while (1)
        {
            build_frame(frame, seq++);

            int sent = send(client_fd, frame, TELEMETRY_FRAME_LEN, 0);
            if (sent != TELEMETRY_FRAME_LEN)
            {
                ESP_LOGW(TAG, "telemetry client disconnected sent=%d errno=%d", sent, errno);
                break;
            }

            /* Fixed 40 ms cadence: delay relative to the previous wake time. */
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
        }

        close(client_fd);
    }
}

void telemetry_server_start(void)
{
    xTaskCreate(telemetry_task, "telemetry", 4096, NULL, 4, NULL);
}
