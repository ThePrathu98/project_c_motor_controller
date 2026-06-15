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
 * HAL rule:
 *   Higher-level code should not directly know GPIO numbers.
 *   app_main/control_task only asks this HAL for encoder counts.
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
 * Optional debug helpers for comparing firmware reads with Saleae channels.
 * These are not needed for the RPM calculation, but they make logs easier to
 * connect to D2/D3 in the Saleae capture.
 */
int encoder_hal_read_a(void);
int encoder_hal_read_b(void);

#endif