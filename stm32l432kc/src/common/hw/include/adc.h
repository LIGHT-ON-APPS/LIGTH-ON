/*
 * adc.h
 *
 * ADC driver
 */

#ifndef SRC_COMMON_HW_INCLUDE_ADC_H_
#define SRC_COMMON_HW_INCLUDE_ADC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_ADC

#define ADC_MAX_CH                  HW_ADC_MAX_CH
#define ADC_SAMPLE_HZ               1000U

typedef void (*adc_update_callback_t)(void);

bool adcInit(void);
bool adcIsInit(void);
bool adcUpdate(void);
void adcSetUpdateCallback(adc_update_callback_t callback);

/* Optional display name for a channel, e.g. "SIGNAL1". Shown by the "adc"
 * CLI so a caller-assigned channel meaning stays visible next to the raw
 * value, without this generic driver knowing what that meaning is. */
void adcSetChannelLabel(uint8_t ch, const char *label);

int32_t adcRead(uint8_t ch);
int32_t adcRead8(uint8_t ch);
int32_t adcRead10(uint8_t ch);
int32_t adcRead12(uint8_t ch);
int32_t adcRead16(uint8_t ch);
uint8_t adcGetRes(uint8_t ch);

float adcReadVoltage(uint8_t ch);
float adcConvVoltage(uint8_t ch, uint32_t adc_value);
uint32_t adcReadMillivolts(uint8_t ch);
uint32_t adcConvMillivolts(uint8_t ch, uint32_t adc_value);

#endif


#ifdef __cplusplus
}
#endif

#endif /* SRC_COMMON_HW_INCLUDE_ADC_H_ */
