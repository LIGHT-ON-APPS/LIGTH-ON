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

/*
 * Pedal-travel scaling, both ends measured on the car 2026-08-07.
 * Released: signal1 106..109 (0.088 V), signal2 79..80 (0.064 V).
 * Fully pressed: signal1 2740 (2.208 V), signal2 2075 (1.672 V).
 *
 * Readings outside raw_min..raw_max clamp to 0% / 100%, so no extra deadband
 * is needed at either end. Signal1 swings about 1.32x signal2; each channel is
 * normalised to per-mille before the plausibility comparison, so the differing
 * gains are expected and do not raise DIFF_FAULT.
 */

// apps에서 adc 컷오프 설정 값
#define APPS_SIGNAL1_RAW_MIN_DEFAULT       121U       //0.085v
#define APPS_SIGNAL1_RAW_MAX_DEFAULT       2740U      //2.208v
#define APPS_SIGNAL2_RAW_MIN_DEFAULT       83U        //0.064v
#define APPS_SIGNAL2_RAW_MAX_DEFAULT       2070U      //1.672v

/*
 * Independent electrical plausibility limits. A reading outside this window
 * raises SIGNALn_RANGE_FAULT, which is how an open or shorted sensor line is
 * caught. The lower limit must sit below the released-pedal reading above, so
 * a sensor that rests near 0 V leaves almost no room to detect a short to
 * ground. Raise these once the sensor's real rest offset is confirmed.
 */

//apps에서 adc상에서 유효하게 찍히는 전압의 최대 최솟값
#define APPS_SIGNAL1_VALID_RAW_MIN_DEFAULT 0U
#define APPS_SIGNAL1_VALID_RAW_MAX_DEFAULT 3685U
#define APPS_SIGNAL2_VALID_RAW_MIN_DEFAULT 0U
#define APPS_SIGNAL2_VALID_RAW_MAX_DEFAULT 2792U

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

/*
 * Installed drivetrain command direction. Keep the pedal and cutoff math as a
 * positive magnitude, then apply this sign only to the command sent on CAN.
 * Use -1 to reverse the motor direction, or +1 for the original direction.
 */
#define APPS_TORQUE_COMMAND_SIGN           (-1)
#define APPS_TORQUE_FULL_COMMAND           \
  (APPS_TORQUE_COMMAND_SIGN * APPS_TORQUE_MAX)

/*
 * Anything at or below this is sent as 0. Unit: Bamocar raw command.
 * 1500 is about 4.6% of APPS_TORQUE_MAX, so ADC noise around a released pedal
 * cannot leave a standing torque request on the bus.
 */
/*
 * Anything at or above this is sent as APPS_TORQUE_MAX. Unit: Bamocar raw
 * command. Mechanical stop tolerance and sensor spread keep a fully pressed
 * pedal from reaching the calibrated maximum (measured 99.1%), so without this
 * the driver could never request full torque. 32112 is about 98%.
 */
// apps에서 토크 컷오프 설정 값
#define APPS_TORQUE_CUTOFF                 1500
#define APPS_TORQUE_CUTOFF_HIGH            32112

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

#endif /* SRC_AP_SW_APPS_APPS_H_ */
