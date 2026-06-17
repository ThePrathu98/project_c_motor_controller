#include "led_hal.h"

#include "driver/gpio.h"
#include "esp_log.h"

#define LED_GPIO        2U
#define LED_ACTIVE_LOW  1

static const char *TAG = "led_hal";

static void led_write(int on)
{
#if LED_ACTIVE_LOW
    gpio_set_level(LED_GPIO, on ? 0 : 1);
#else
    gpio_set_level(LED_GPIO, on ? 1 : 0);
#endif
}

void led_hal_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&cfg);
    led_write(0);

    ESP_LOGI(TAG, "GPIO2 onboard LED initialized active-low");
}

void led_hal_update(led_hal_mode_t mode, uint32_t tick_ms)
{
    switch (mode)
    {
        case LED_HAL_OFF:
            led_write(0);
            break;

        case LED_HAL_SOLID:
            led_write(1);
            break;

        case LED_HAL_SLOW_BLINK:
            /* 1 Hz blink: 500 ms on / 500 ms off. */
            led_write(((tick_ms / 500U) % 2U) == 0U);
            break;

        case LED_HAL_FAST_BLINK:
            /* 5 Hz blink: 100 ms on / 100 ms off. */
            led_write(((tick_ms / 100U) % 2U) == 0U);
            break;

        default:
            led_write(0);
            break;
    }
}
