/*
 * apps.c
 */

#include "apps.h"
#include "gpio.h"

#ifdef _USE_HW_CLI
#include "cli.h"
#define APPS_TRACE_BAR_LEN     16
#define APPS_TRACE_PERIOD_MS   50
#define APPS_MONITOR_PERIOD_MS 100

typedef enum
{
  APPS_CUTOFF_NONE = 0,
  APPS_CUTOFF_LOW,
  APPS_CUTOFF_HIGH,
} apps_cutoff_t;

typedef struct
{
  uint32_t observed_status;
  apps_cutoff_t cutoff;
} apps_cli_state_t;
#endif

#define APPS_IMMEDIATE_FAULT_MASK  (APPS_STATUS_NOT_CONFIGURED | APPS_STATUS_ADC_READ_FAULT)
#define APPS_RANGE_FAULT_MASK      (APPS_STATUS_SIGNAL1_RANGE_FAULT | APPS_STATUS_SIGNAL2_RANGE_FAULT)
#define APPS_TIMED_FAULT_MASK      (APPS_STATUS_DIFF_FAULT | APPS_RANGE_FAULT_MASK)

//
typedef struct
{
  uint32_t bad_sample_count;			// 이상한 샘플이 몇개 있는지 셈 100회가 약 100ms
  uint32_t pending_status;				// 100ms가 되지는 않았지만 고장 중인 상태
  uint32_t active_status;				// 100ms 채운 상태
} apps_fault_policy_t;

typedef struct
{
  uint16_t raw_min[APPS_ADC_MAX];			// 페달 0%로 환산할 ADC 기준값
  uint16_t raw_max[APPS_ADC_MAX];			// 페달 100%로 환산할 ADC 기준값
  uint16_t valid_raw_min[APPS_ADC_MAX];		// 단선·단락 판정용 허용 ADC 최솟값
  uint16_t valid_raw_max[APPS_ADC_MAX];		// 단선·단락 판정용 허용 ADC 최댓값

  uint16_t raw[APPS_ADC_MAX];				// 메인 루프가 마지막으로 읽은 실제 ADC값
  uint16_t percent_per_mille[APPS_ADC_MAX];	// 두 채널을 각각 0~1000으로 환산한 값
  uint16_t pedal_per_mille;					// 두 채널 중 작은 값으로 결정한 최종 페달값

  int16_t  command_sent;					// 컷오프와 방향 부호까지 적용해 실제 전송한 토크

  uint32_t status;							// 외부로 보여주는 종합 상태 비트
  bool     adc_ok;							// 마지막 ADC 읽기가 성공했는지
  bool     valid;							// 현재 페달값을 토크에 사용해도 되는지

  bool     can_tx_ok;						// 마지막 CAN 송신이 성공했는지
  bool     can_tx_muted;					// 고장 때문에 CAN 채널을 의도적으로 닫았는지

  volatile apps_fault_policy_t fault_policy;// 100ms 고장 판정용 카운터와 상태

  volatile uint32_t isr_fault_status;				// ISR이 가장 최근 샘플에서 본 고장
  volatile uint32_t isr_actionable_fault_events;	//
  volatile bool     isr_output_inhibit;				//

  volatile uint32_t isr_adc_sequence;
  uint32_t adc_sequence_seen;
  uint32_t adc_sequence_seen_ms;

  uint32_t fault_latched_status;

  bool     initialized;

} apps_t;

static apps_t apps;

#ifdef _USE_HW_CLI

static apps_cli_state_t apps_cli;
static bool apps_command_registered = false;

static void appsCli(cli_args_t *args);
#endif

static void appsProcessAdcStatus(uint32_t status);
static uint32_t appsCheckPolicy(uint16_t raw_signal1,
                                uint16_t raw_signal2,
                                uint16_t *p_signal1_per_mille,
                                uint16_t *p_signal2_per_mille);

// 1ms 마다 안전 검사해주는 콜백 함수
static void appsAdcCallback(void)
{
  uint16_t signal1_per_mille;
  uint16_t signal2_per_mille;
  int32_t raw_signal1;
  int32_t raw_signal2;
  uint32_t status;

  raw_signal1 = adcRead(APPS_SIGNAL1_ADC_CH);
  raw_signal2 = adcRead(APPS_SIGNAL2_ADC_CH);

  if ((raw_signal1 < 0) || (raw_signal1 > UINT16_MAX) ||
      (raw_signal2 < 0) || (raw_signal2 > UINT16_MAX))
  {
    appsProcessAdcStatus(APPS_STATUS_ADC_READ_FAULT);
    return;
  }

  status = appsCheckPolicy((uint16_t)raw_signal1,
                           (uint16_t)raw_signal2,
                           &signal1_per_mille,
                           &signal2_per_mille);

  appsProcessAdcStatus(status);
}


/* Fail-safe status output: both APPS plausibility and the latest CAN TX must pass. */
static void appsUpdateReadyGpio(void)
{
  uint32_t primask;
  bool ready;

  /* Keep the read-and-write atomic against the ADC ISR's fail-low write. */
  primask = __get_PRIMASK();
  __disable_irq();
  ready = (apps.valid == true) && (apps.can_tx_ok == true) &&
		  (apps.can_tx_muted != true) && (apps.isr_output_inhibit != true);

  gpioPinWrite(APPS_READY_GPIO_CH, ready);

  if (primask == 0U)
  {
    __enable_irq();
  }
}
/*
 * 확정된 고장이 래치돼 있는지. 예전 fault_latched 플래그를 대신한다.
 * 래치 원인이 곧 래치 여부이므로 둘을 따로 들고 있을 이유가 없다.
 */
static bool appsFaultIsLatched(void)
{
  return (apps.fault_latched_status != APPS_STATUS_OK);
}

/* Called once per coherent ADC pair from the 1 kHz DMA completion ISR. */
static uint32_t appsFaultPolicyUpdate(volatile apps_fault_policy_t *p_policy, uint32_t observed_status)
{
  uint32_t immediate_status = observed_status & APPS_IMMEDIATE_FAULT_MASK;
  uint32_t delayed_status 	= observed_status & APPS_TIMED_FAULT_MASK;
  uint32_t confirmed_status = APPS_STATUS_OK;
  uint32_t actionable_status;

  if (delayed_status == APPS_STATUS_OK)
  {
	  p_policy->bad_sample_count = 0U;
	  p_policy->pending_status = APPS_STATUS_OK;
  }
  else
  {
    if (p_policy->bad_sample_count == 0U)
    {
      p_policy->bad_sample_count = 1U;
      p_policy->pending_status = delayed_status;
    }
    else
    {
      if (p_policy->bad_sample_count < APPS_DIFF_CONFIRM_SAMPLES)
      {
        p_policy->bad_sample_count++;
      }
      p_policy->pending_status |= delayed_status;
    }

    if (p_policy->bad_sample_count >= APPS_DIFF_CONFIRM_SAMPLES)
    {
      confirmed_status = p_policy->pending_status;
    }
  }

  actionable_status = immediate_status | confirmed_status;

  /* Process a sticky actionable event before allowing a healthy reset. */
  if (actionable_status != APPS_STATUS_OK)
  {
    p_policy->active_status |= actionable_status;
  }
  else if (observed_status == APPS_STATUS_OK)
  {
    p_policy->active_status = APPS_STATUS_OK;
  }

  return p_policy->active_status;
}

bool appsInit(void)
{
  memset(&apps, 0, sizeof(apps));
#ifdef _USE_HW_CLI
  memset(&apps_cli, 0, sizeof(apps_cli));
#endif

  apps.raw_min[APPS_SIGNAL1] = APPS_SIGNAL1_RAW_MIN_DEFAULT;
  apps.raw_max[APPS_SIGNAL1] = APPS_SIGNAL1_RAW_MAX_DEFAULT;
  apps.raw_min[APPS_SIGNAL2] = APPS_SIGNAL2_RAW_MIN_DEFAULT;
  apps.raw_max[APPS_SIGNAL2] = APPS_SIGNAL2_RAW_MAX_DEFAULT;

  apps.valid_raw_min[APPS_SIGNAL1] = APPS_SIGNAL1_VALID_RAW_MIN_DEFAULT;
  apps.valid_raw_max[APPS_SIGNAL1] = APPS_SIGNAL1_VALID_RAW_MAX_DEFAULT;
  apps.valid_raw_min[APPS_SIGNAL2] = APPS_SIGNAL2_VALID_RAW_MIN_DEFAULT;
  apps.valid_raw_max[APPS_SIGNAL2] = APPS_SIGNAL2_VALID_RAW_MAX_DEFAULT;

  apps.initialized = true;

  adcSetChannelLabel(APPS_SIGNAL1_ADC_CH, "SIGNAL1");
  adcSetChannelLabel(APPS_SIGNAL2_ADC_CH, "SIGNAL2");

  adcSetUpdateCallback(appsAdcCallback);
  apps.status = APPS_STATUS_OK;
  appsUpdateReadyGpio();

#ifdef _USE_HW_CLI
  if (apps_command_registered != true)
  {
    apps_command_registered = cliAdd("apps", appsCli);
  }
#endif

  return true;
}

static void appsProcessAdcStatus(uint32_t status)
{
  uint32_t effective_status;

  effective_status = appsFaultPolicyUpdate(&apps.fault_policy, status);
  apps.isr_fault_status = status;

  if (effective_status != APPS_STATUS_OK)
  {
    apps.isr_actionable_fault_events |= effective_status;
    apps.isr_output_inhibit = true;

    /* GPIO access is bounded and ISR-safe; CAN transmission is deferred. */
    gpioPinWrite(APPS_READY_GPIO_CH, false);
  }

  /* Publish sequence last so a main-loop snapshot never sees half an update. */
  apps.isr_adc_sequence++;
}

static bool appsReleaseOutputIfSafe(void)
{
  uint32_t primask;
  bool released;

  primask = __get_PRIMASK();
  __disable_irq();

  if ((apps.isr_output_inhibit == true) &&
      (apps.isr_fault_status == APPS_STATUS_OK) &&
      (apps.fault_policy.active_status == APPS_STATUS_OK) &&
      (apps.isr_actionable_fault_events == APPS_STATUS_OK))
  {
    apps.isr_output_inhibit = false;
  }
  released = (apps.isr_output_inhibit != true);

  if (primask == 0U)
  {
    __enable_irq();
  }

  return released;
}

/* 컷오프시 adc를 0으로 만들어 줌 */
static void appsInvalidateResult(void)
{
  memset(apps.raw, 0, sizeof(apps.raw));
  memset(apps.percent_per_mille, 0, sizeof(apps.percent_per_mille));

  apps.pedal_per_mille 		= 0U;
  apps.command_sent 		= 0;
  apps.adc_ok 				= false;
  apps.valid 				= false;
  apps.can_tx_ok 			= false;
  apps.can_tx_muted 		= false;
}
// adc 아날로그 값을 디지털로 인식하게 만들어줌
static uint16_t appsRawToDigital(uint8_t signal, uint16_t raw, bool *p_range_ok)
{
  int32_t raw_min 	= apps.raw_min[signal];
  int32_t raw_max 	= apps.raw_max[signal];
  int32_t span 		= raw_max - raw_min;
  int32_t scaled;

  *p_range_ok = (raw >= apps.valid_raw_min[signal]) && (raw <= apps.valid_raw_max[signal]);

  scaled = (((int32_t)raw - raw_min) * (int32_t)APPS_PER_MILLE_MAX) / span;

  if (scaled < 0)
  {
    return 0U;
  }
  if (scaled > (int32_t)APPS_PER_MILLE_MAX)
  {
    return APPS_PER_MILLE_MAX;
  }

  return (uint16_t)scaled;
}
// 비타당성 검사
static uint32_t appsCheckPolicy(uint16_t raw_signal1,
                                uint16_t raw_signal2,
                                uint16_t *p_signal1_per_mille,
                                uint16_t *p_signal2_per_mille)
{
  bool signal1_range_ok = false;
  bool signal2_range_ok = false;
  uint16_t difference_per_mille;
  uint32_t status = APPS_STATUS_OK;

  *p_signal1_per_mille = appsRawToDigital(APPS_SIGNAL1, raw_signal1, &signal1_range_ok);
  *p_signal2_per_mille = appsRawToDigital(APPS_SIGNAL2, raw_signal2, &signal2_range_ok);

  if (signal1_range_ok != true)
  {
    status |= APPS_STATUS_SIGNAL1_RANGE_FAULT;
  }
  if (signal2_range_ok != true)
  {
    status |= APPS_STATUS_SIGNAL2_RANGE_FAULT;
  }

  if (*p_signal1_per_mille > *p_signal2_per_mille)
  {
    difference_per_mille = *p_signal1_per_mille - *p_signal2_per_mille;
  }
  else
  {
    difference_per_mille = *p_signal2_per_mille - *p_signal1_per_mille;
  }

  if (difference_per_mille >= APPS_DIFF_MAX)
  {
    status |= APPS_STATUS_DIFF_FAULT;
  }

  return status;
}

static void appsUpdateFaultStatus(uint32_t observed_status, uint32_t effective_status)
{
  apps.fault_latched_status = effective_status;
#ifdef _USE_HW_CLI
  apps_cli.observed_status = observed_status;
#endif

  apps.status = observed_status | effective_status;
  if (effective_status != APPS_STATUS_OK)
  {
    apps.status |= APPS_STATUS_FAULT_LATCHED;
  }
}
// 즉시 fault
static void appsRaiseImmediateFault(uint32_t observed_status)
{
  uint32_t immediate_status;
  uint32_t effective_status;
  uint32_t primask;

  immediate_status = observed_status & APPS_IMMEDIATE_FAULT_MASK;
  if (immediate_status == APPS_STATUS_OK)
  {
    return;
  }

  primask = __get_PRIMASK();
  __disable_irq();

  apps.fault_policy.active_status |= immediate_status;
  effective_status = apps.fault_policy.active_status;

  apps.isr_actionable_fault_events |= immediate_status;
  apps.isr_output_inhibit = true;

  /* This is the bounded fail-low action; CAN zero is sent by the main loop. */
  gpioPinWrite(APPS_READY_GPIO_CH, false);
  appsUpdateFaultStatus(observed_status, effective_status);

  if (primask == 0U)
  {
    __enable_irq();
  }
}
/*
 *메인 루프의 판정의 본체
 *여기서 fault판정을 내림
 *여기서
 */
static bool appsEvaluate(uint16_t raw_signal1, uint16_t raw_signal2)
{
  uint32_t instant_status 		= APPS_STATUS_OK;
  uint32_t isr_observed_status;
  uint32_t isr_effective_status;
  uint32_t isr_actionable_events;
  uint32_t effective_status;
  uint32_t primask;

  primask = __get_PRIMASK();
  __disable_irq();

  isr_observed_status 					= apps.isr_fault_status;
  isr_effective_status 					= apps.fault_policy.active_status;
  isr_actionable_events 				= apps.isr_actionable_fault_events;

  apps.isr_actionable_fault_events 		= APPS_STATUS_OK;

  if (primask == 0U)
  {
    __enable_irq();
  }

  apps.raw[APPS_SIGNAL1] 				= raw_signal1;
  apps.raw[APPS_SIGNAL2] 				= raw_signal2;

  apps.percent_per_mille[APPS_SIGNAL1] = 0U;
  apps.percent_per_mille[APPS_SIGNAL2] = 0U;

  apps.pedal_per_mille 					= 0U;
  apps.command_sent 					= 0;
  apps.adc_ok 							= true;
  apps.valid 							= false;
  apps.can_tx_ok 						= false;
  apps.can_tx_muted 					= false;

  if (apps.initialized != true)
  {
    return false;
  }

  instant_status = appsCheckPolicy(raw_signal1,
                                   raw_signal2,
                                   &apps.percent_per_mille[APPS_SIGNAL1],
                                   &apps.percent_per_mille[APPS_SIGNAL2]);

  instant_status |= isr_observed_status;
  instant_status |= isr_actionable_events;

  apps.pedal_per_mille =
      (apps.percent_per_mille[APPS_SIGNAL1] < apps.percent_per_mille[APPS_SIGNAL2]) ? apps.percent_per_mille[APPS_SIGNAL1] : apps.percent_per_mille[APPS_SIGNAL2];

  effective_status = isr_effective_status |
                     isr_actionable_events |
                     (instant_status & APPS_IMMEDIATE_FAULT_MASK);

  appsUpdateFaultStatus(instant_status, effective_status);

  if ((effective_status == APPS_STATUS_OK) &&
      (appsReleaseOutputIfSafe() == true))
  {
    apps.valid = true;
  }

  return true;
}
// 지금 값들이 어떠나 찍는거
bool appsGetSnapshot(apps_snapshot_t *p_snapshot)
{
  if ((p_snapshot == NULL) || (apps.initialized != true))
  {
    return false;
  }

  p_snapshot->raw[APPS_SIGNAL1] 				= apps.raw[APPS_SIGNAL1];
  p_snapshot->raw[APPS_SIGNAL2] 				= apps.raw[APPS_SIGNAL2];
  p_snapshot->percent_per_mille[APPS_SIGNAL1] 	= apps.percent_per_mille[APPS_SIGNAL1];
  p_snapshot->percent_per_mille[APPS_SIGNAL2] 	= apps.percent_per_mille[APPS_SIGNAL2];
  p_snapshot->pedal_per_mille 					= apps.pedal_per_mille;
  p_snapshot->command_sent 						= apps.command_sent;
  p_snapshot->status 							= apps.status;
  p_snapshot->fault_latched_status 				= apps.fault_latched_status;
  p_snapshot->adc_ok 							= apps.adc_ok;
  p_snapshot->valid 							= apps.valid;
  p_snapshot->can_tx_ok 						= apps.can_tx_ok;
  p_snapshot->can_tx_muted 						= apps.can_tx_muted;
  p_snapshot->fault_latched 					= appsFaultIsLatched();

  return true;
}

// adc를 수집하고 범위 이탈 및 안 움직이면 ISR이 죽은 것으로 보고 adc고장을 냄
static bool appsUpdate(void)
{
  int32_t raw_signal1;
  int32_t raw_signal2;
  uint32_t adc_sequence;
  uint32_t now_ms;

  if (apps.initialized != true)     return false;

  // 1. adc값을 받아들이지 못할때 즉 adc에 이상이 있는경우
  if (adcUpdate() != true)
  {
    uint32_t status = APPS_STATUS_ADC_READ_FAULT;

    appsInvalidateResult();
    appsRaiseImmediateFault(status);

    return false;
  }

  now_ms = millis();
  adc_sequence = apps.isr_adc_sequence;

  // 2. 애매한디?
  if (adc_sequence == apps.adc_sequence_seen)
  {
    if ((uint32_t)(now_ms - apps.adc_sequence_seen_ms) >= APPS_ADC_STALE_MS)
    {
      appsInvalidateResult();
      appsRaiseImmediateFault(APPS_STATUS_ADC_READ_FAULT);
      return false;
    }
  }
  else
  {
    apps.adc_sequence_seen = adc_sequence;
    apps.adc_sequence_seen_ms = now_ms;
  }

  raw_signal1 = adcRead(APPS_SIGNAL1_ADC_CH);
  raw_signal2 = adcRead(APPS_SIGNAL2_ADC_CH);

  // 3. 0과 4096 즉 나올 수 없는 값이 나오는 경우
  if ((raw_signal1 < 0) || (raw_signal1 > UINT16_MAX) || (raw_signal2 < 0) || (raw_signal2 > UINT16_MAX))
  {
    appsInvalidateResult();
    appsRaiseImmediateFault(APPS_STATUS_ADC_READ_FAULT);
    return false;
  }

  return appsEvaluate((uint16_t)raw_signal1, (uint16_t)raw_signal2);
}

/*
① 무효 상태면 빌드 버전에 따라 갈린다 — _DEF_VER1은 CAN 채널을 닫고 mute, _DEF_VER2는 토크 0을 계속 송신.
② 양끝 컷오프
③ 판정과 송신 사이에 ISR이 끼어들었는지 다시 확인해 레이스를 닫는다.
④ APPS_TORQUE_COMMAND_SIGN으로 방향을 붙여 송신하고 결과를 can_tx_ok에 기록한다.
 */
static bool appsSendCommand(int16_t prepared_command)
{
  int16_t command_to_send;

  if (apps.initialized != true)
  {
    return false;
  }

  if ((apps.valid != true) || (apps.isr_output_inhibit == true))
  {
#if _DEF_VER == _DEF_VER1
    if (canIsOpen(APPS_CAN_CH) == true)
    {
      canClose(APPS_CAN_CH);
    }

    apps.command_sent       = 0;
    apps.can_tx_ok          = false;
    apps.can_tx_muted       = true;
    apps.status            &= ~APPS_STATUS_CAN_TX_FAULT;
    return true;
#elif _DEF_VER == _DEF_VER2
    command_to_send = 0;
#endif
  }
  else
  {
    command_to_send = prepared_command;
  }

  /*
   * Snap both ends of the range: the noise floor to a clean zero, and the top
   * to full torque so mechanical tolerance cannot cap the driver below 100%.
   */
#ifdef _USE_HW_CLI
  apps_cli.cutoff = APPS_CUTOFF_NONE;
#endif
  if ((command_to_send > 0) && (command_to_send <= APPS_TORQUE_CUTOFF))
  {
    command_to_send = 0;
#ifdef _USE_HW_CLI
    apps_cli.cutoff = APPS_CUTOFF_LOW;
#endif
  }
  else if (command_to_send >= APPS_TORQUE_CUTOFF_HIGH)
  {
    command_to_send = APPS_TORQUE_MAX;
#ifdef _USE_HW_CLI
    apps_cli.cutoff = APPS_CUTOFF_HIGH;
#endif
  }

  /* Close the evaluate-to-send race if the ADC ISR inhibits in between. */
  if (apps.isr_output_inhibit == true)
  {
    command_to_send = 0;
    apps.valid = false;
#ifdef _USE_HW_CLI
    apps_cli.cutoff = APPS_CUTOFF_NONE;
#endif
  }

  command_to_send = (int16_t)((int32_t)command_to_send *
                              (int32_t)APPS_TORQUE_COMMAND_SIGN);

  if (canIsOpen(APPS_CAN_CH) != true)
  {
    (void)canOpen(APPS_CAN_CH, CAN_NORMAL, CAN_CLASSIC, CAN_500K, CAN_500K);
  }

  apps.command_sent = command_to_send;
  apps.can_tx_muted = false;
  apps.can_tx_ok = bamocarCanSendTorqueCmd(APPS_CAN_CH,
                                           command_to_send);

  if (apps.can_tx_ok != true)
  {
    apps.status |= APPS_STATUS_CAN_TX_FAULT;
    return false;
  }

  apps.status &= ~APPS_STATUS_CAN_TX_FAULT;
  return true;
}

// 토크만큼 곱해서 지령을 내리게하는 함수
static int16_t appsMakePedalCommand(void)
{
  uint32_t command;

  command = ((uint32_t)apps.pedal_per_mille * (uint32_t)APPS_TORQUE_MAX) / (uint32_t)APPS_PER_MILLE_MAX;

  if (command > (uint32_t)INT16_MAX)
  {
    command = (uint32_t)INT16_MAX;
  }

  return (int16_t)command;
}
/*
 * adc값을 업데이트하고
 * 지령을 만들고
 * 값을 보냄
 */
bool appsRunPedalCommand(void)
{
  bool adc_result             = appsUpdate();
  int16_t pedal_command       = appsMakePedalCommand();
  bool can_result             = appsSendCommand(pedal_command);

  appsUpdateReadyGpio();

  return adc_result && can_result;
}

#ifdef _USE_HW_CLI

static bool appsFaultTimerActive(void)
{
  return (apps.fault_policy.bad_sample_count != 0U);
}

/* Absolute APPS1/APPS2 gap in per-mille, the value compared to APPS_DIFF_MAX. */
static uint16_t appsCliDiffPerMille(void)
{
  if (apps.percent_per_mille[APPS_SIGNAL1] > apps.percent_per_mille[APPS_SIGNAL2])
  {
    return apps.percent_per_mille[APPS_SIGNAL1] - apps.percent_per_mille[APPS_SIGNAL2];
  }

  return apps.percent_per_mille[APPS_SIGNAL2] - apps.percent_per_mille[APPS_SIGNAL1];
}

/*
 * One word for the whole plausibility state machine, shared by status,
 * monitor and verify so the three views never disagree.
 */
static const char *appsCliStateName(void)
{
  if (appsFaultIsLatched() == true)
  {
    return "FAULT";
  }
  if (appsFaultTimerActive() == true)
  {
    return "PENDING";
  }
  if (apps.valid != true)
  {
    return "INVALID";
  }

  return "OK";
}

/* The fault timer never reads past the confirm time once the fault is latched. */
static uint32_t appsCliFaultTimerMs(void)
{
  uint32_t bad_sample_count;
  uint32_t elapsed_ms;

  /* ISR이 갱신하는 카운터라 한 번만 읽어 값이 갈라지지 않게 한다. */
  bad_sample_count = apps.fault_policy.bad_sample_count;
  elapsed_ms = (bad_sample_count * 1000U) / ADC_SAMPLE_HZ;

  if (elapsed_ms > APPS_FAULT_MS)
  {
    return APPS_FAULT_MS;
  }

  return elapsed_ms;
}

static const char *appsCliDiffStateName(void)
{
  if ((apps_cli.observed_status & APPS_STATUS_DIFF_FAULT) == 0U)
  {
    return ((apps.fault_latched_status & APPS_STATUS_DIFF_FAULT) != 0U) ?
           "HOLD" : "OK";
  }
  if ((apps.fault_latched_status & APPS_STATUS_DIFF_FAULT) != 0U)
  {
    return "FAULT";
  }

  return "PENDING";
}

static const char *appsCliCanText(void)
{
  if (apps.can_tx_muted == true)
  {
    return "MUTE";
  }

  return (apps.can_tx_ok == true) ? "OK" : "FAIL";
}

/*
 * Active fault bits, one name per line. The first name continues the caller's
 * line, every following name is indented to sit under it.
 */
static void appsCliPrintFaultNames(uint32_t status, const char *p_indent)
{
  bool printed = false;

  if ((status & APPS_STATUS_NOT_CONFIGURED) != 0U)
  {
    cliPrintf("%sNOT_CONFIGURED\n", printed ? p_indent : "");
    printed = true;
  }
  if ((status & APPS_STATUS_ADC_READ_FAULT) != 0U)
  {
    cliPrintf("%sADC_READ_FAULT\n", printed ? p_indent : "");
    printed = true;
  }
  if ((status & APPS_STATUS_SIGNAL1_RANGE_FAULT) != 0U)
  {
    cliPrintf("%sSIGNAL1_RANGE_FAULT\n", printed ? p_indent : "");
    printed = true;
  }
  if ((status & APPS_STATUS_SIGNAL2_RANGE_FAULT) != 0U)
  {
    cliPrintf("%sSIGNAL2_RANGE_FAULT\n", printed ? p_indent : "");
    printed = true;
  }
  if ((status & APPS_STATUS_DIFF_FAULT) != 0U)
  {
    cliPrintf("%sDIFF_FAULT\n", printed ? p_indent : "");
    printed = true;
  }
  if ((status & APPS_STATUS_CAN_TX_FAULT) != 0U)
  {
    cliPrintf("%sCAN_TX_FAULT\n", printed ? p_indent : "");
    printed = true;
  }

  if (printed != true)
  {
    cliPrintf("NONE\n");
  }
}

static void appsCli(cli_args_t *args)
{
	// 1회 출력
  if ((args->argc == 1) && args->isStr(0, "status"))
  {
    uint32_t status;
    uint32_t input_status;
    uint16_t difference_per_mille;
    uint16_t signal1_mv;
    uint16_t signal2_mv;

    (void)appsRunPedalCommand();

    status               = apps.status;
    input_status         = apps_cli.observed_status;
    difference_per_mille = appsCliDiffPerMille();
    signal1_mv           = (uint16_t)adcConvMillivolts(APPS_SIGNAL1_ADC_CH,
                                                       apps.raw[APPS_SIGNAL1]);
    signal2_mv           = (uint16_t)adcConvMillivolts(APPS_SIGNAL2_ADC_CH,
                                                       apps.raw[APPS_SIGNAL2]);

    cliPrintf("========== APPS STATUS ==========\n");
    cliPrintf("STATE        : %s\n", appsCliStateName());
    cliPrintf("\n");
    cliPrintf("SIGNAL1      : raw=%-5u %u.%03uV %5u.%u%%   [%s]\n",
              apps.raw[APPS_SIGNAL1],
              signal1_mv / 1000U, signal1_mv % 1000U,
              apps.percent_per_mille[APPS_SIGNAL1] / 10U,
              apps.percent_per_mille[APPS_SIGNAL1] % 10U,
              ((input_status & APPS_STATUS_SIGNAL1_RANGE_FAULT) != 0U) ? "RANGE" : "OK");
    cliPrintf("SIGNAL2      : raw=%-5u %u.%03uV %5u.%u%%   [%s]\n",
              apps.raw[APPS_SIGNAL2],
              signal2_mv / 1000U, signal2_mv % 1000U,
              apps.percent_per_mille[APPS_SIGNAL2] / 10U,
              apps.percent_per_mille[APPS_SIGNAL2] % 10U,
              ((input_status & APPS_STATUS_SIGNAL2_RANGE_FAULT) != 0U) ? "RANGE" : "OK");
    cliPrintf("\n");
    cliPrintf("DIFFERENCE   : %u.%u%% / %u.%u%%      [%s]\n",
              difference_per_mille / 10U, difference_per_mille % 10U,
              APPS_DIFF_MAX / 10U, APPS_DIFF_MAX % 10U,
              appsCliDiffStateName());
    cliPrintf("\n");
    cliPrintf("INPUT NOW    : ");
    appsCliPrintFaultNames(input_status, "               ");
    cliPrintf("INHIBIT CAUSE: ");
    appsCliPrintFaultNames(apps.fault_latched_status, "               ");
    cliPrintf("FAULT TIMER  : %lu / %lu ms\n",
              (unsigned long)appsCliFaultTimerMs(),
              (unsigned long)APPS_FAULT_MS);
    cliPrintf("FAILURE      : %s\n", appsFaultIsLatched() ? "ACTIVE" : "NONE");
    cliPrintf("STATUS       : 0x%08lX\n", (unsigned long)status);
    cliPrintf("\n");
    cliPrintf("PEDAL        : %u.%u%%\n",
              apps.pedal_per_mille / 10U, apps.pedal_per_mille % 10U);
    cliPrintf("COMMAND      : %d / %d\n",
              apps.command_sent, APPS_TORQUE_FULL_COMMAND);
    cliPrintf("READY        : %s\n",
              gpioPinRead(APPS_READY_GPIO_CH) ? "HIGH" : "LOW");
    cliPrintf("CAN TX       : %s\n", appsCliCanText());
    cliPrintf("=================================\n");
    return;
  }

  if ((args->argc == 1) && args->isStr(0, "monitor"))
  {
	// 100ms 마다 반복
    cliPrintf("APPS MONITOR - press any key to stop\n");
    cliPrintf("--------------------------------------------------------------------------------\n");
    cliPrintf("STATE       S1       S2      DIFF      TIMER  VALID     CMD  READY   CAN\n");
    cliPrintf("--------------------------------------------------------------------------------\n");

    while (cliKeepLoop() == true)
    {
      uint16_t difference_per_mille;

      (void)appsRunPedalCommand();

      difference_per_mille = appsCliDiffPerMille();

      cliPrintf("%-9s %4u.%u%% %6u.%u%% %6u.%u%% %6lu/%-4lu %4s %7d %6s %5s\n",
                appsCliStateName(),
                apps.percent_per_mille[APPS_SIGNAL1] / 10U,
                apps.percent_per_mille[APPS_SIGNAL1] % 10U,
                apps.percent_per_mille[APPS_SIGNAL2] / 10U,
                apps.percent_per_mille[APPS_SIGNAL2] % 10U,
                difference_per_mille / 10U,
                difference_per_mille % 10U,
                (unsigned long)appsCliFaultTimerMs(),
                (unsigned long)APPS_FAULT_MS,
                apps.valid ? "YES" : "NO",
                apps.command_sent,
                gpioPinRead(APPS_READY_GPIO_CH) ? "HIGH" : "LOW",
                appsCliCanText());

      for (uint32_t wait_ms = 0U;
           (wait_ms < APPS_MONITOR_PERIOD_MS) && (cliKeepLoop() == true);
           wait_ms++)
      {
        delay(1);
      }
    }
    return;
  }

  /* Field demonstration for the separate immediate and delayed policies. */
  if ((args->argc == 1) && args->isStr(0, "verify"))
  {
    uint32_t start_ms;
    uint32_t prev_effective_status = APPS_STATUS_OK;
    bool     first_sample          = true;
    bool     prev_pending          = false;
    bool     seen_pending          = false;
    bool     seen_diff_confirmed   = false;
    bool     seen_range_confirmed  = false;
    bool     seen_immediate        = false;
    bool     seen_torque_zero      = false;
    bool     seen_ready_low        = false;
    bool     seen_recovered        = false;
    bool     can_ok                = true;
    uint32_t pending_ms            = 0U;

    cliPrintf("========================================\n");
    cliPrintf(" APPS PLAUSIBILITY VERIFICATION\n");
    cliPrintf("========================================\n");
    cliPrintf("Diff limit      : %u.%u %%\n",
              APPS_DIFF_MAX / 10U, APPS_DIFF_MAX % 10U);
    cliPrintf("Valid raw S1    : %u .. %u\n",
              apps.valid_raw_min[APPS_SIGNAL1],
              apps.valid_raw_max[APPS_SIGNAL1]);
    cliPrintf("Valid raw S2    : %u .. %u\n",
              apps.valid_raw_min[APPS_SIGNAL2],
              apps.valid_raw_max[APPS_SIGNAL2]);
    cliPrintf("Rule limit      : %lu ms (DIFF and RANGE)\n",
              (unsigned long)APPS_FAULT_MS);
    cliPrintf("ISR confirm     : %lu samples @ %lu Hz\n",
              (unsigned long)APPS_DIFF_CONFIRM_SAMPLES,
              (unsigned long)ADC_SAMPLE_HZ);
    cliPrintf("ADC/cfg fault   : immediate\n");
    cliPrintf("CAN channel     : %u\n", APPS_CAN_CH);
    cliPrintf("\n");
    cliPrintf("Disconnect or short an APPS signal to begin test.\n");
    cliPrintf("Press any key to finish.\n\n");

    /* Started after the banner so the log begins at 0 ms, not at the
       time the header took to go out over the UART. */
    start_ms = millis();

    while (cliKeepLoop() == true)
    {
      uint32_t now_ms;
      uint32_t status;
      uint32_t effective_status;
      uint32_t immediate_status;
      uint16_t difference_per_mille;
      bool     new_actionable_fault = false;

      (void)appsRunPedalCommand();

      now_ms               = millis() - start_ms;
      status               = apps.status;
      effective_status     = apps.fault_latched_status;
      immediate_status     = effective_status & APPS_IMMEDIATE_FAULT_MASK;
      difference_per_mille = appsCliDiffPerMille();

      if ((apps.can_tx_muted != true) && (apps.can_tx_ok != true))
      {
        can_ok = false;
      }

      if (((seen_diff_confirmed == true) ||
           (seen_range_confirmed == true) ||
           (seen_immediate == true)) &&
          (apps.valid == true) &&
          (gpioPinRead(APPS_READY_GPIO_CH) == true))
      {
        seen_recovered = true;
      }

      /* Baseline line, so the log shows what the pedal read before the test. */
      if (first_sample == true)
      {
        cliPrintf("[%06lu ms] %s\n", (unsigned long)now_ms,
                  (apps.valid == true) ? "NORMAL" : appsCliStateName());
        cliPrintf("            S1=%u.%u%%  S2=%u.%u%%  DIFF=%u.%u%%\n\n",
                  apps.percent_per_mille[APPS_SIGNAL1] / 10U,
                  apps.percent_per_mille[APPS_SIGNAL1] % 10U,
                  apps.percent_per_mille[APPS_SIGNAL2] / 10U,
                  apps.percent_per_mille[APPS_SIGNAL2] % 10U,
                  difference_per_mille / 10U,
                  difference_per_mille % 10U);
        first_sample = false;
      }

      if ((appsFaultTimerActive() == true) && (prev_pending != true))
      {
        seen_pending = true;

        cliPrintf("[%06lu ms] IMPLAUSIBILITY DETECTED\n",
                  (unsigned long)now_ms);
        cliPrintf("            S1=%u.%u%%%s\n",
                  apps.percent_per_mille[APPS_SIGNAL1] / 10U,
                  apps.percent_per_mille[APPS_SIGNAL1] % 10U,
                  ((status & APPS_STATUS_SIGNAL1_RANGE_FAULT) != 0U) ? " [RANGE]" : "");
        cliPrintf("            S2=%u.%u%%%s\n",
                  apps.percent_per_mille[APPS_SIGNAL2] / 10U,
                  apps.percent_per_mille[APPS_SIGNAL2] % 10U,
                  ((status & APPS_STATUS_SIGNAL2_RANGE_FAULT) != 0U) ? " [RANGE]" : "");
        cliPrintf("            DIFF=%u.%u%%%s\n",
                  difference_per_mille / 10U, difference_per_mille % 10U,
                  ((status & APPS_STATUS_DIFF_FAULT) != 0U) ? " [PENDING]" : "");
        cliPrintf("[%06lu ms] %lu ms TIMER START\n\n",
                  (unsigned long)now_ms, (unsigned long)APPS_FAULT_MS);
      }

      if ((immediate_status != APPS_STATUS_OK) &&
          ((prev_effective_status & APPS_IMMEDIATE_FAULT_MASK) == APPS_STATUS_OK))
      {
        seen_immediate = true;
        new_actionable_fault = true;

        cliPrintf("[%06lu ms] IMMEDIATE ADC/CONFIG FAILURE\n",
                  (unsigned long)now_ms);
        cliPrintf("            ");
        appsCliPrintFaultNames(immediate_status, "            ");
      }

      if (((effective_status & APPS_STATUS_DIFF_FAULT) != 0U) &&
          ((prev_effective_status & APPS_STATUS_DIFF_FAULT) == 0U))
      {
        seen_diff_confirmed = true;
        new_actionable_fault = true;

        cliPrintf("[%06lu ms] DIFF FAILURE CONFIRMED\n",
                  (unsigned long)now_ms);
        cliPrintf("            elapsed=%lu ms\n",
                  (unsigned long)appsCliFaultTimerMs());
      }

      if (((effective_status & APPS_RANGE_FAULT_MASK) != 0U) &&
          ((prev_effective_status & APPS_RANGE_FAULT_MASK) == 0U))
      {
        seen_range_confirmed = true;
        new_actionable_fault = true;

        cliPrintf("[%06lu ms] RANGE FAILURE CONFIRMED\n",
                  (unsigned long)now_ms);
        cliPrintf("            ");
        appsCliPrintFaultNames(effective_status & APPS_RANGE_FAULT_MASK,
                               "            ");
        cliPrintf("            elapsed=%lu ms\n",
                  (unsigned long)appsCliFaultTimerMs());
      }

      if (new_actionable_fault == true)
      {
        if (apps.command_sent == 0)
        {
          seen_torque_zero = true;
        }
        cliPrintf("[%06lu ms] TORQUE INHIBITED\n", (unsigned long)now_ms);
        cliPrintf("            command=%d\n", apps.command_sent);

        if (gpioPinRead(APPS_READY_GPIO_CH) != true)
        {
          seen_ready_low = true;
        }
        cliPrintf("[%06lu ms] READY GPIO -> %s\n",
                  (unsigned long)now_ms,
                  gpioPinRead(APPS_READY_GPIO_CH) ? "HIGH" : "LOW");
        cliPrintf("[%06lu ms] CAN TX -> %s\n\n",
                  (unsigned long)now_ms, appsCliCanText());
      }

      /* DIFF and RANGE share the 100 ms timer and its recovery. */
      if ((appsFaultTimerActive() != true) && (prev_pending == true))
      {
        cliPrintf("[%06lu ms] IMPLAUSIBILITY RETURNED WITHIN LIMIT\n",
                  (unsigned long)now_ms);
        cliPrintf("            S1=%u.%u%%\n",
                  apps.percent_per_mille[APPS_SIGNAL1] / 10U,
                  apps.percent_per_mille[APPS_SIGNAL1] % 10U);
        cliPrintf("            S2=%u.%u%%\n",
                  apps.percent_per_mille[APPS_SIGNAL2] / 10U,
                  apps.percent_per_mille[APPS_SIGNAL2] % 10U);
        cliPrintf("            DIFF=%u.%u%%\n",
                  difference_per_mille / 10U, difference_per_mille % 10U);

        if ((prev_effective_status & APPS_TIMED_FAULT_MASK) == 0U)
        {
          cliPrintf("[%06lu ms] TIMER RESET (%lu / %lu ms)\n",
                    (unsigned long)now_ms,
                    (unsigned long)pending_ms,
                    (unsigned long)APPS_FAULT_MS);
          cliPrintf("            FAILURE NOT CONFIRMED\n\n");
        }
        else
        {
          if ((effective_status == APPS_STATUS_OK) && (apps.valid == true) &&
              (gpioPinRead(APPS_READY_GPIO_CH) == true))
          {
            cliPrintf("[%06lu ms] FAILURE CLEARED\n", (unsigned long)now_ms);
            cliPrintf("[%06lu ms] TORQUE RE-ENABLED\n", (unsigned long)now_ms);
            cliPrintf("            command=%d\n", apps.command_sent);
            cliPrintf("[%06lu ms] READY GPIO -> HIGH\n\n",
                      (unsigned long)now_ms);
          }
          else
          {
            cliPrintf("            OUTPUT INHIBIT HELD: OTHER APPS FAULT ACTIVE\n\n");
          }
        }
      }

      if ((immediate_status == APPS_STATUS_OK) &&
          ((prev_effective_status & APPS_IMMEDIATE_FAULT_MASK) != APPS_STATUS_OK))
      {
        cliPrintf("[%06lu ms] ADC/CONFIG FAILURE CLEARED\n",
                  (unsigned long)now_ms);
        if ((effective_status != APPS_STATUS_OK) || (apps.valid != true))
        {
          cliPrintf("            OUTPUT INHIBIT HELD: APPS FAULT EPISODE ACTIVE\n");
        }
      }

      if (appsFaultTimerActive() == true)
      {
        pending_ms = appsCliFaultTimerMs();
      }
      prev_pending = appsFaultTimerActive();
      prev_effective_status = effective_status;

      for (uint32_t wait_ms = 0U;
           (wait_ms < APPS_RUN_MS) && (cliKeepLoop() == true);
           wait_ms++)
      {
        delay(1);
      }
    }

    cliPrintf("\n========================================\n");
    cliPrintf(" TEST RESULT\n");
    cliPrintf("========================================\n");
    cliPrintf("Immediate ADC/config failure : %s\n",
              seen_immediate ? "PASS" : "NOT SEEN");
    cliPrintf("Implausibility pending       : %s\n",
              seen_pending ? "PASS" : "NOT SEEN");
    cliPrintf("DIFF confirmed >= %lu ms     : %s\n",
              (unsigned long)APPS_FAULT_MS,
              (seen_diff_confirmed == true) ? "PASS" : "NOT SEEN");
    cliPrintf("RANGE confirmed >= %lu ms    : %s\n",
              (unsigned long)APPS_FAULT_MS,
              (seen_range_confirmed == true) ? "PASS" : "NOT SEEN");
    cliPrintf("Torque command inhibited     : %s\n",
              seen_torque_zero ? "PASS" : "FAIL");
    cliPrintf("READY GPIO LOW               : %s\n",
              seen_ready_low ? "PASS" : "FAIL");
    cliPrintf("CAN transmission             : %s\n",
              can_ok ? "PASS" : "FAIL");
    cliPrintf("Recovered after fault clears : %s\n",
              ((seen_immediate != true) &&
               (seen_diff_confirmed != true) &&
               (seen_range_confirmed != true)) ?
              "NOT TESTED" : (seen_recovered ? "PASS" : "NOT OBSERVED"));
    cliPrintf("\n");

    if ((seen_immediate != true) && (seen_pending != true))
    {
      cliPrintf("RESULT                       : NO FAULT INJECTED\n");
    }
    else if (((seen_immediate == true) ||
              (seen_diff_confirmed == true) ||
              (seen_range_confirmed == true)) &&
             (seen_torque_zero == true) && (seen_ready_low == true) &&
             (can_ok == true))
    {
      cliPrintf("RESULT                       : PASS\n");
    }
    else
    {
      cliPrintf("RESULT                       : FAIL\n");
    }
    cliPrintf("========================================\n");
    return;
  }

  if ((args->argc == 1) && args->isStr(0, "config"))
  {
    cliPrintf("initialized : %s\n",
              apps.initialized ? "true" : "false");
    cliPrintf("signal1 adc/raw : ch%u, %u..%u\n",
              APPS_SIGNAL1_ADC_CH,
              apps.raw_min[APPS_SIGNAL1],
              apps.raw_max[APPS_SIGNAL1]);
    cliPrintf("signal2 adc/raw : ch%u, %u..%u\n",
              APPS_SIGNAL2_ADC_CH,
              apps.raw_min[APPS_SIGNAL2],
              apps.raw_max[APPS_SIGNAL2]);
    cliPrintf("signal1 valid raw : %u..%u\n",
              apps.valid_raw_min[APPS_SIGNAL1],
              apps.valid_raw_max[APPS_SIGNAL1]);
    cliPrintf("signal2 valid raw : %u..%u\n",
              apps.valid_raw_min[APPS_SIGNAL2],
              apps.valid_raw_max[APPS_SIGNAL2]);
    cliPrintf("can channel : %u\n", APPS_CAN_CH);
    cliPrintf("max difference : %u.%u%%\n",
              APPS_DIFF_MAX / 10U,
              APPS_DIFF_MAX % 10U);
    cliPrintf("diff rule limit : %lums\n", (unsigned long)APPS_FAULT_MS);
    cliPrintf("diff isr confirm : %lu samples @ %luHz\n",
              (unsigned long)APPS_DIFF_CONFIRM_SAMPLES,
              (unsigned long)ADC_SAMPLE_HZ);
    cliPrintf("range/adc action : immediate\n");
    cliPrintf("torque cutoff : <=%d, full >=%d, max %d\n",
              APPS_TORQUE_CUTOFF, APPS_TORQUE_CUTOFF_HIGH, APPS_TORQUE_MAX);
    return;
  }

  /*
   * One line per sample so a pedal press and release can be watched live.
   * Tracks the travel actually reached and how often the cutoff fired, then
   * prints a summary when the user stops it with a key press.
   */
  if ((args->argc == 1) && args->isStr(0, "trace"))
  {
    uint16_t pedal_min    = (uint16_t)APPS_PER_MILLE_MAX;
    uint16_t pedal_max    = 0U;
    int16_t  command_min  = 0;
    int16_t  command_max  = 0;
    uint32_t cutoff_count = 0U;
    uint32_t full_count   = 0U;
    uint32_t sample_count = 0U;

    cliPrintf("cutoff <= %d, full >= %d, max %d\n",
              APPS_TORQUE_CUTOFF, APPS_TORQUE_CUTOFF_HIGH, APPS_TORQUE_MAX);
    cliPrintf("press any key to stop\n\n");
    cliPrintf(" pedal     cmd  0%%              100%%  state\n");

    while (cliKeepLoop() == true)
    {
      char bar[APPS_TRACE_BAR_LEN + 1U];
      uint8_t filled;
      const char *state;

      (void)appsRunPedalCommand();

      filled = (uint8_t)(((uint32_t)apps.pedal_per_mille *
                          (uint32_t)APPS_TRACE_BAR_LEN) /
                         (uint32_t)APPS_PER_MILLE_MAX);

      for (uint8_t i = 0U; i < APPS_TRACE_BAR_LEN; i++)
      {
        bar[i] = (i < filled) ? '#' : '.';
      }
      bar[APPS_TRACE_BAR_LEN] = '\0';

      if (apps.valid != true)
      {
        state = "FAULT";
      }
      else if (apps_cli.cutoff == APPS_CUTOFF_LOW)
      {
        state = "CUT";
      }
      else if (apps_cli.cutoff == APPS_CUTOFF_HIGH)
      {
        state = "FULL";
      }
      else if (apps.command_sent == 0)
      {
        /* Nothing to cut: the pedal is already at rest. */
        state = "IDLE";
      }
      else
      {
        state = "RUN";
      }

      cliPrintf("%3u.%u%%  %6d  |%s| %s\n",
                apps.pedal_per_mille / 10U,
                apps.pedal_per_mille % 10U,
                apps.command_sent,
                bar,
                state);

      if (apps.pedal_per_mille < pedal_min)
      {
        pedal_min = apps.pedal_per_mille;
      }
      if (apps.pedal_per_mille > pedal_max)
      {
        pedal_max = apps.pedal_per_mille;
      }
      if (apps.command_sent > command_max)
      {
        command_max = apps.command_sent;
      }
      if (apps.command_sent < command_min)
      {
        command_min = apps.command_sent;
      }
      if (apps_cli.cutoff == APPS_CUTOFF_LOW)
      {
        cutoff_count++;
      }
      if (apps_cli.cutoff == APPS_CUTOFF_HIGH)
      {
        full_count++;
      }
      sample_count++;

      for (uint32_t wait_ms = 0U;
           (wait_ms < APPS_TRACE_PERIOD_MS) && (cliKeepLoop() == true);
           wait_ms++)
      {
        delay(1);
      }
    }

    cliPrintf("\n--- summary ---\n");
    cliPrintf("samples : %lu\n", (unsigned long)sample_count);
    cliPrintf("pedal   : %u.%u%% .. %u.%u%%\n",
              pedal_min / 10U, pedal_min % 10U,
              pedal_max / 10U, pedal_max % 10U);
    cliPrintf("command : %d .. %d / %d\n",
              command_min, command_max, APPS_TORQUE_FULL_COMMAND);
    cliPrintf("cutoff  : %lu samples\n", (unsigned long)cutoff_count);
    cliPrintf("full    : %lu samples\n", (unsigned long)full_count);
    return;
  }

  cliPrintf("apps status                     : print current state once\n");
  cliPrintf("apps monitor                    : live state, %ums period\n",
            APPS_MONITOR_PERIOD_MS);
  cliPrintf("apps verify                     : plausibility verification mode\n");
  cliPrintf("apps trace                      : pedal / torque cutoff trace\n");
  cliPrintf("apps config                     : print thresholds and ADC setup\n");
  cliPrintf("apps help                       : this menu\n");
}

#endif
