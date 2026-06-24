#ifndef ENCODER_HAL_H
#define ENCODER_HAL_H

#include <stdint.h>

/*
 * encoder_hal.h
 *
 * Public HAL interface for the quadrature encoder.
 *
 * The hardware pins are intentionally hidden in encoder_hal.c so the control
 * code can stay independent from ESP8266 GPIO numbers.
 *
 * Hardware pin choices stay in encoder_hal.c; control code only sees counts.
 */

/* Configure encoder GPIO inputs and install the GPIO ISR handler. */
void encoder_hal_init(void);

/*
 * Returns signed encoder delta since the previous call, then resets it.
 *
 * Example:
 *   If this returns 120 during a 100 ms window, then 120 encoder edges
 *   were counted during that sample window.
 */
int32_t encoder_hal_get_and_reset_delta(void);

/*
 * Debug helpers for comparing firmware GPIO reads with Saleae D2/D3.
 */
int encoder_hal_read_a(void);
int encoder_hal_read_b(void);

#endif