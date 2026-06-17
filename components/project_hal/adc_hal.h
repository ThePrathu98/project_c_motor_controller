#ifndef ADC_HAL_H
#define ADC_HAL_H

#include <stdint.h>

/*
 * adc_hal.h
 *
 * DRV8870EVM current-sense input wrapper.
 *
 * Hardware used in this build:
 *   DRV8870EVM ISEN silver test pad -> ESP8266 NodeMCU A0
 *   R_ISEN on the EVM is R150 = 0.150 ohm
 *
 * Current estimate:
 *   current_mA = isen_mV * 1000 / 150
 */

void adc_hal_init(void);
uint16_t adc_hal_read_raw(void);
uint16_t adc_hal_read_mv(void);
int32_t adc_hal_read_current_ma(void);

#endif /* ADC_HAL_H */
