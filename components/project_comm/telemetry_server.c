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
 * telemetry_server.c
 *
 * Binary telemetry stream for host-side tools.
 *
 * Day 3-4 used human-readable ESP_LOGI and text STATUS replies. Day 5-6 adds a
 * separate binary stream so the PC can receive fixed-size samples at 100 Hz
 * without parsing long text strings.
 *
 * TCP port: 5006
 * Rate:     100 Hz, because TELEMETRY_PERIOD_MS = 10
 * Frame:    fixed 20-byte little-endian uint8_t buffer
 *
 * Host Python unpack format:
 *   <IhhhhBBHI
 *
 * Frame layout:
 *   byte  0..3   uint32_t sequence number
 *   byte  4..5   int16_t target RPM
 *   byte  6..7   int16_t actual RPM
 *   byte  8..9   int16_t duty permille, e.g. 710 means 71.0 %
 *   byte 10..11  int16_t current in mA
 *   byte 12      uint8_t state enum
 *   byte 13      uint8_t fault flags
 *   byte 14..15  uint16_t missed control-deadline counter, low 16 bits
 *   byte 16..19  uint32_t uptime/control tick in ms
 *
 * The sequence number lets the host detect dropped frames or reconnect gaps.
 * The firmware keeps the frame as a fixed uint8_t buffer; no dynamic strings
 * are allocated in this telemetry path.
 */

#define TELEMETRY_PORT      5006
#define TELEMETRY_PERIOD_MS 10
#define TELEMETRY_FRAME_LEN 20

static const char *TAG = "telemetry_server";

static void put_u16_le(uint8_t *buf, uint16_t v)
{
    buf[0] = (uint8_t)(v & 0xFFU);
    buf[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static void put_i16_le(uint8_t *buf, int16_t v)
{
    /*
     * Signed values are stored using the same two-byte little-endian layout.
     * Casting to uint16_t preserves the two's-complement bit pattern.
     */
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
    /*
     * control_get_status() returns a snapshot of the control task's current
     * variables. telemetry_server.c observes these values only; it does not
     * command speed, clear faults, or touch PWM.
     */
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
        /*
         * Accept one telemetry client at a time. When the PC script exits or
         * Wi-Fi drops, send() fails, we close the client, and accept() waits for
         * the next connection. This is the manual reconnect path verified in
         * the Day 5-6 evidence logs.
         */
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

            /*
             * vTaskDelayUntil() keeps a fixed 10 ms cadence relative to the
             * previous wake time, which is better for stable sample timing than
             * delaying 10 ms after the send work finishes.
             */
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
        }

        close(client_fd);
    }
}

void telemetry_server_start(void)
{
    xTaskCreate(telemetry_task, "telemetry", 4096, NULL, 4, NULL);
}
