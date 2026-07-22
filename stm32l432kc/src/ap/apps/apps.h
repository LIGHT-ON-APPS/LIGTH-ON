/*
 * apps.h
 *
 * Accelerator Pedal Position Sensor application logic.
 */

#ifndef SRC_AP_APPS_APPS_H_
#define SRC_AP_APPS_APPS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"
#include "adc.h"
#include "bamocar_can.h"


#define APPS_ADC_MAX                       HW_ADC_MAX_CH
#define APPS_PER_MILLE_MAX                 1000U

#define APPS_SIGNAL1_ADC_CH                _DEF_ADC1
#define APPS_SIGNAL2_ADC_CH                _DEF_ADC2

#define APPS_CAN_CH                        _DEF_CAN1

#define APPS_SIGNAL1_RAW_MIN_DEFAULT       0U
#define APPS_SIGNAL1_RAW_MAX_DEFAULT       3102U      //2.5v
#define APPS_SIGNAL2_RAW_MIN_DEFAULT       0U
#define APPS_SIGNAL2_RAW_MAX_DEFAULT       4095U      //3.3v


/* ADC raw tolerance below calibrated min. Unit: ADC count. */
#define APPS_RAW_LOW_TOL                   80U

/* ADC raw tolerance above calibrated max. Unit: ADC count. */
#define APPS_RAW_HIGH_TOL                  80U

/* Max allowed APPS1/APPS2 difference. Unit: per-mille, 100 = 10.0%. */
#define APPS_DIFF_MAX                      100U

/* Fault must last this long before it is confirmed. Unit: ms. */
#define APPS_FAULT_MS                      100U

/* APPS control loop period. Unit: ms. */
#define APPS_RUN_MS                        10U

/* Torque command sent at 100% pedal. Unit: Bamocar raw command. */
#define APPS_TORQUE_MAX                    BAMOCAR_TORQUE_CMD_MAX

typedef enum
{
  APPS_SIGNAL1 = 0,
  APPS_SIGNAL2,
} apps_signal_t;

typedef enum
{
  APPS_STATUS_OK                  = 0x00000000U,
  APPS_STATUS_NOT_CONFIGURED      = 0x00000001U,
  APPS_STATUS_ADC_READ_FAULT      = 0x00000002U,
  APPS_STATUS_SIGNAL1_RANGE_FAULT = 0x00000004U,
  APPS_STATUS_SIGNAL2_RANGE_FAULT = 0x00000008U,
  APPS_STATUS_DIFF_FAULT          = 0x00000010U,
  APPS_STATUS_FAULT_LATCHED       = 0x00000020U,
  APPS_STATUS_CAN_TX_FAULT        = 0x00000040U,
} apps_status_t;

typedef struct
{
  uint16_t raw[APPS_ADC_MAX];
  uint16_t percent_per_mille[APPS_ADC_MAX];
  uint16_t pedal_per_mille;
  int16_t  command_sent;
  uint32_t status;
  uint32_t fault_latched_status;
  bool     adc_ok;
  bool     valid;
  bool     can_tx_ok;
  bool     can_tx_muted;
  bool     fault_latched;
  bool     configured;
} apps_snapshot_t;


bool appsInit(void);
bool appsSetConfig(uint16_t signal1_raw_min,
                   uint16_t signal1_raw_max,
                   uint16_t signal2_raw_min,
                   uint16_t signal2_raw_max);
void appsClearFault(void);
bool appsGetSnapshot(apps_snapshot_t *p_snapshot);

bool appsRunPedalCommand(void);


#ifdef __cplusplus
}
#endif

#endif /* SRC_AP_APPS_APPS_H_ */
