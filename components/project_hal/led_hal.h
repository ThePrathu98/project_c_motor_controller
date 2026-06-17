#ifndef LED_HAL_H
#define LED_HAL_H

#include <stdint.h>

/* GPIO2 onboard LED is active-low on the ESP8266 NodeMCU. */
typedef enum
{
    LED_HAL_OFF = 0,
    LED_HAL_SOLID,
    LED_HAL_SLOW_BLINK,
    LED_HAL_FAST_BLINK
} led_hal_mode_t;

void led_hal_init(void);
void led_hal_update(led_hal_mode_t mode, uint32_t tick_ms);

#endif /* LED_HAL_H */
