#include "encoder_hal.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"

/*
 * encoder_hal.c
 *
 * Quadrature encoder input wrapper.
 *
 * GPIO5 counts Encoder A edges. GPIO4 is sampled inside the ISR to determine
 * direction. The control task reads a signed delta once per speed sample.
 */

/*
 * Wiring:
 *   Encoder A / yellow -> NodeMCU D1 -> GPIO5
 *   Encoder B / white  -> NodeMCU D2 -> GPIO4
 */
#define ENCODER_A_GPIO  5
#define ENCODER_B_GPIO  4

static const char *TAG = "encoder_hal";

/*
 * File-local encoder count.
 *
 * volatile:
 *   Modified inside ISR and read/reset from FreeRTOS task.
 */
static volatile int32_t s_encoder_delta = 0;

/*
 * Direction sign correction.
 *
 * If positive motor duty produces negative RPM after this change,
 * flip this from 1 to -1.
 */
static volatile int32_t s_dir_sign = 1;

/*
 * Encoder A interrupt handler.
 *
 * IRAM_ATTR:
 *   Places ISR code in instruction RAM.
 *
 * ISR rule:
 *   Keep it short. No logging, malloc, printf, or blocking calls here.
 */
static void IRAM_ATTR encoder_a_isr(void *arg)
{
    (void)arg;

    /* Read both phases at the edge instant to infer rotation direction. */
    int a = gpio_get_level(ENCODER_A_GPIO);
    int b = gpio_get_level(ENCODER_B_GPIO);

    if (a == b)
    {
        s_encoder_delta += s_dir_sign;
    }
    else
    {
        s_encoder_delta -= s_dir_sign;
    }
}


/* Initialize encoder GPIOs and attach the interrupt handler. */
void encoder_hal_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ENCODER_A_GPIO) | (1ULL << ENCODER_B_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "gpio_config failed err=%d", err);
        return;
    }

    /*
     * Install GPIO ISR service.
     *
     * ESP_ERR_INVALID_STATE means it was already installed.
     */
    err = gpio_install_isr_service(0);
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE))
    {
        ESP_LOGE(TAG, "gpio_install_isr_service failed err=%d", err);
        return;
    }

    /*
     * Attach ISR only to Encoder A.
     *
     * Encoder B is still read inside the ISR for direction/reference.
     */
    gpio_set_intr_type(ENCODER_A_GPIO, GPIO_INTR_ANYEDGE);
    gpio_set_intr_type(ENCODER_B_GPIO, GPIO_INTR_DISABLE);

    err = gpio_isr_handler_add(ENCODER_A_GPIO, encoder_a_isr, NULL);
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE))
    {
        ESP_LOGE(TAG, "gpio_isr_handler_add A failed err=%d", err);
        return;
    }

    ESP_LOGI(TAG, "Encoder A-edge counting enabled: A=GPIO5, B=GPIO4");
}

/*
 * Return the signed count accumulated since the previous sample and then clear
 * it. control_task.c uses this as one RPM measurement window.
 */
int32_t encoder_hal_get_and_reset_delta(void)
{
    int32_t delta = s_encoder_delta;
    s_encoder_delta = 0;
    return delta;
}

/* Debug helper used in serial telemetry to compare against Saleae D2. */
int encoder_hal_read_a(void)
{
    return gpio_get_level(ENCODER_A_GPIO);
}

/* Debug helper used in serial telemetry to compare against Saleae D3. */
int encoder_hal_read_b(void)
{
    return gpio_get_level(ENCODER_B_GPIO);
}