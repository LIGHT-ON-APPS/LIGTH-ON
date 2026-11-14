/*
 * apps.h
 *
 * Accelerator Pedal Position Sensor application logic.
 */

#ifndef SRC_AP_SW_APPS_APPS_H_
#define SRC_AP_SW_APPS_APPS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"
#include "adc.h"
#include "sw/bamocar/bamocar_can.h"


#define APPS_ADC_MAX                       HW_ADC_MAX_CH
#define APPS_PER_MILLE_MAX                 1000U

#define APPS_SIGNAL1_ADC_CH                _DEF_ADC2
#define APPS_SIGNAL2_ADC_CH                _DEF_ADC1

#define APPS_CAN_CH                        _DEF_CAN1

/* High only while APPS plausibility is valid and the latest CAN TX succeeds. */
#define APPS_READY_GPIO_CH                 _DEF_GPIO1

// apps에서 adc 컷오프 설정 값
#define APPS_SIGNAL1_RAW_MIN_DEFAULT       610U       //0.419v
#define APPS_SIGNAL1_RAW_MAX_DEFAULT       3700U      //3.050v
#define APPS_SIGNAL2_RAW_MIN_DEFAULT       458U        //0.322v
#define APPS_SIGNAL2_RAW_MAX_DEFAULT       2812U      //2.320v

//apps에서 adc상에서 유효하게 찍히는 전압의 최대 최솟값
#define APPS_SIGNAL1_VALID_RAW_MIN_DEFAULT 	200U		// 0.16v
#define APPS_SIGNAL1_VALID_RAW_MAX_DEFAULT 	4050U		// 3.26v
#define APPS_SIGNAL2_VALID_RAW_MIN_DEFAULT 	200U		// 0.16v
#define APPS_SIGNAL2_VALID_RAW_MAX_DEFAULT 	4050U		// 3.26v

/* Max allowed APPS1/APPS2 difference. Unit: per-mille, 100 = 10.0%. */
#define APPS_DIFF_MAX                      100U

/* APPS control loop period. Unit: ms. */
#define APPS_RUN_MS                        10U
#define APPS_ADC_STALE_MS                  APPS_RUN_MS

/* Regulatory maximum for continuous APPS implausibility. Unit: ms. */
#define APPS_FAULT_MS                      100U

/* Number of consecutive 1 kHz ADC samples spanning the 100 ms rule window. */
#define APPS_DIFF_CONFIRM_SAMPLES          \
  (((APPS_FAULT_MS * ADC_SAMPLE_HZ) + 999U) / 1000U)

/* Torque command sent at 100% pedal. Unit: Bamocar raw command. */
#define APPS_TORQUE_MAX                    BAMOCAR_TORQUE_CMD_MAX


#define APPS_TORQUE_COMMAND_SIGN           (-1)
#define APPS_TORQUE_FULL_COMMAND           \
  (APPS_TORQUE_COMMAND_SIGN * APPS_TORQUE_MAX)

// apps에서 토크 컷오프 설정 값
#define APPS_TORQUE_CUTOFF                 1500
#define APPS_TORQUE_CUTOFF_HIGH            32767

typedef enum
{
  APPS_SIGNAL1 = 0,
  APPS_SIGNAL2,
} apps_signal_t;

typedef enum
{
  APPS_STATUS_OK                  = 0x00000000U, // 고장이 없는 정상 상태
  APPS_STATUS_NOT_CONFIGURED      = 0x00000001U, // APPS 설정값이 준비되지 않은 상태
  APPS_STATUS_ADC_READ_FAULT      = 0x00000002U, // ADC 값을 읽지 못했거나 갱신이 멈춘 상태
  APPS_STATUS_SIGNAL1_RANGE_FAULT = 0x00000004U, // 1번 센서 ADC 값이 허용 범위를 벗어난 상태
  APPS_STATUS_SIGNAL2_RANGE_FAULT = 0x00000008U, // 2번 센서 ADC 값이 허용 범위를 벗어난 상태
  APPS_STATUS_DIFF_FAULT          = 0x00000010U, // 두 센서 값의 차이가 허용치를 넘은 상태
  APPS_STATUS_FAULT_LATCHED       = 0x00000020U, // 확정된 고장이 현재 적용 중인 상태
  APPS_STATUS_CAN_TX_FAULT        = 0x00000040U, // 토크 명령 CAN 전송에 실패한 상태
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
} apps_snapshot_t;


bool appsInit(void);

bool appsGetSnapshot(apps_snapshot_t *p_snapshot);

bool appsRunPedalCommand(void);


#ifdef __cplusplus
}
#endif

#endif /* SRC_AP_SW_APPS_APPS_H_ */
