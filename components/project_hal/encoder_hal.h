#ifndef ENCODER_HAL_H
#define ENCODER_HAL_H

/*
 * encoder_hal.h
 *
 * Public interface for encoder input hardware.
 *
 * Current Day 2 version:
 *   only reads raw A/B pin levels.
 *
 * Later version:
 *   will add GPIO interrupts and quadrature counting.
 */

void encoder_hal_init(void);

int encoder_hal_read_a(void);
int encoder_hal_read_b(void);

#endif