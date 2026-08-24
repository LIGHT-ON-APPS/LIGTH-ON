/*
 * apps.c
 */

#include "apps.h"
#include "gpio.h"

#ifdef _USE_HW_CLI
#include "cli.h"
#endif


#define APPS_IMMEDIATE_FAULT_MASK  (APPS_STATUS_NOT_CONFIGURED      | \
                                    APPS_STATUS_ADC_READ_FAULT      | \
                                    APPS_STATUS_SIGNAL1_RANGE_FAULT | \
                                    APPS_STATUS_SIGNAL2_RANGE_FAULT)

#define APPS_TIMED_FAULT_MASK      APPS_STATUS_DIFF_FAULT

_Static_assert(APPS_DIFF_CONFIRM_SAMPLES > 0U,
               "APPS DIFF confirmation needs at least one ADC sample");
_Static_assert((APPS_IMMEDIATE_FAULT_MASK & APPS_TIMED_FAULT_MASK) == 0U,
               "APPS immediate and timed fault masks must not overlap");

#if (_DEF_VER != _DEF_VER1) && (_DEF_VER != _DEF_VER2)
#error "_DEF_VER must be _DEF_VER1 or _DEF_VER2 for APPS fault output"
#endif

#if (APPS_TORQUE_COMMAND_SIGN != -1) && (APPS_TORQUE_COMMAND_SIGN != 1)
#error "APPS_TORQUE_COMMAND_SIGN must be -1 or 1"
#endif

/* "apps trace" live view: bar width in characters and sample period in ms. */
#define APPS_TRACE_BAR_LEN     16U
#define APPS_TRACE_PERIOD_MS   50U

/* "apps monitor" live table refresh period in ms. */
#define APPS_MONITOR_PERIOD_MS 100U

/* Which end of the torque range the last CAN command was snapped to. */
typedef enum
{
  APPS_CUTOFF_NONE = 0,
  APPS_CUTOFF_LOW,
  APPS_CUTOFF_HIGH,
} apps_cutoff_t;

/*
 * The 1 kHz ADC completion ISR normally owns this policy. Main-loop ADC
 * watchdog failures join the same episode only while IRQs are masked.
 * Counting consecutive samples avoids the 10 ms cooperative-main-loop
 * quantisation that previously led to an internal timing compensation.
 */
typedef struct
{
  uint32_t bad_sample_count;
  uint32_t elapsed_ms;
  uint32_t pending_status;
  uint32_t confirmed_status;
  uint32_t active_status;
  bool     timer_active;
  bool     failure_active;
} apps_fault_policy_t;

typedef struct
{
  uint32_t observed_status;
  uint32_t effective_status;
  uint32_t actionable_events;
} apps_isr_fault_snapshot_t;

typedef struct
{
  uint16_t raw_min[APPS_ADC_MAX];
  uint16_t raw_max[APPS_ADC_MAX];
  uint16_t valid_raw_min[APPS_ADC_MAX];
  uint16_t valid_raw_max[APPS_ADC_MAX];
  uint16_t raw[APPS_ADC_MAX];
  uint16_t percent_per_mille[APPS_ADC_MAX];
  uint16_t pedal_per_mille;
  int16_t  command_sent;
  uint32_t status;
  bool     adc_ok;
  bool     valid;
  apps_cutoff_t cutoff;
  bool     can_tx_ok;
  bool     can_tx_muted;
  volatile uint32_t isr_fault_status;
  volatile uint32_t isr_effective_fault_status;
  volatile uint32_t isr_actionable_fault_events;
  volatile uint32_t isr_adc_sequence;
  volatile bool     isr_output_inhibit;

  volatile apps_fault_policy_t fault_policy;
  uint32_t adc_sequence_seen;
  uint32_t adc_sequence_seen_ms;
  uint32_t observed_status;
  uint32_t fault_latched_status;
  bool     fault_latched;

  bool     initialized;
  bool     configured;
} apps_t;

static apps_t apps;

/* appsInit()이 정의보다 앞에서 호출하는 내부 함수들. */
static bool appsCalibrationIsValid(void);
static void appsClearFaultTimer(void);
static void appsResetFaultState(void);
static bool appsTryClearFault(void);
static void appsAdcCallback(void);
static void appsUpdateReadyGpio(void);

static void appsFaultPolicyClearDelayed(volatile apps_fault_policy_t *p_policy)
{
  p_policy->bad_sample_count = 0U;
  p_policy->elapsed_ms = 0U;
  p_policy->pending_status = APPS_STATUS_OK;
  p_policy->confirmed_status = APPS_STATUS_OK;
  p_policy->timer_active = false;
}

static void appsFaultPolicyReset(volatile apps_fault_policy_t *p_policy)
{
  appsFaultPolicyClearDelayed(p_policy);
  p_policy->active_status = APPS_STATUS_OK;
  p_policy->failure_active = false;
}

/* Called once per coherent ADC pair from the 1 kHz DMA completion ISR. */
static uint32_t appsFaultPolicyUpdate(volatile apps_fault_policy_t *p_policy,
                                      uint32_t observed_status)
{
  uint32_t immediate_status = observed_status & APPS_IMMEDIATE_FAULT_MASK;
  uint32_t delayed_status = observed_status & APPS_TIMED_FAULT_MASK;
  uint32_t actionable_status;

  if (delayed_status == APPS_STATUS_OK)
  {
    appsFaultPolicyClearDelayed(p_policy);
  }
  else
  {
    if (p_policy->timer_active != true)
    {
      p_policy->bad_sample_count = 1U;
      p_policy->pending_status = delayed_status;
      p_policy->confirmed_status = APPS_STATUS_OK;
      p_policy->timer_active = true;
    }
    else
    {
      if (p_policy->bad_sample_count < APPS_DIFF_CONFIRM_SAMPLES)
      {
        p_policy->bad_sample_count++;
      }
      p_policy->pending_status |= delayed_status;
    }

    p_policy->elapsed_ms =
        (p_policy->bad_sample_count * 1000U) / ADC_SAMPLE_HZ;

    if (p_policy->bad_sample_count >= APPS_DIFF_CONFIRM_SAMPLES)
    {
      p_policy->confirmed_status = p_policy->pending_status;
    }
  }

  actionable_status = immediate_status | p_policy->confirmed_status;

  /* Process a sticky actionable event before allowing a healthy reset. */
  if (actionable_status != APPS_STATUS_OK)
  {
    p_policy->active_status |= actionable_status;
    p_policy->failure_active = true;
  }
  else if (observed_status == APPS_STATUS_OK)
  {
    p_policy->active_status = APPS_STATUS_OK;
    p_policy->failure_active = false;
  }

  return (p_policy->failure_active == true) ?
         p_policy->active_status : APPS_STATUS_OK;
}

#ifdef _USE_HW_CLI
static bool apps_command_registered = false;

static void appsCli(cli_args_t *args);
#endif

bool appsInit(void)
{
  memset(&apps, 0, sizeof(apps));
  appsClearFaultTimer();

  apps.raw_min[APPS_SIGNAL1] = APPS_SIGNAL1_RAW_MIN_DEFAULT;
  apps.raw_max[APPS_SIGNAL1] = APPS_SIGNAL1_RAW_MAX_DEFAULT;
  apps.raw_min[APPS_SIGNAL2] = APPS_SIGNAL2_RAW_MIN_DEFAULT;
  apps.raw_max[APPS_SIGNAL2] = APPS_SIGNAL2_RAW_MAX_DEFAULT;
  apps.valid_raw_min[APPS_SIGNAL1] = APPS_SIGNAL1_VALID_RAW_MIN_DEFAULT;
  apps.valid_raw_max[APPS_SIGNAL1] = APPS_SIGNAL1_VALID_RAW_MAX_DEFAULT;
  apps.valid_raw_min[APPS_SIGNAL2] = APPS_SIGNAL2_VALID_RAW_MIN_DEFAULT;
  apps.valid_raw_max[APPS_SIGNAL2] = APPS_SIGNAL2_VALID_RAW_MAX_DEFAULT;

  apps.configured = appsCalibrationIsValid();
  apps.initialized = true;

  adcSetChannelLabel(APPS_SIGNAL1_ADC_CH, "SIGNAL1");
  adcSetChannelLabel(APPS_SIGNAL2_ADC_CH, "SIGNAL2");

  adcSetUpdateCallback(appsAdcCallback);
  apps.status = (apps.configured == true) ? APPS_STATUS_OK : APPS_STATUS_NOT_CONFIGURED;
  appsUpdateReadyGpio();

#ifdef _USE_HW_CLI
  if (apps_command_registered != true)
  {
    apps_command_registered = cliAdd("apps", appsCli);
  }
#endif

  return true;
}

/* Calibration is invalid if any channel has the same min and max. */
static bool appsCalibrationIsValid(void)
{
  for (uint8_t i = 0; i < APPS_ADC_MAX; i++)
  {
    if ((apps.raw_min[i] == apps.raw_max[i]) ||
        (apps.valid_raw_min[i] > apps.valid_raw_max[i]))
    {
      return false;
    }
  }

  return true;
}

static void appsClearFaultTimer(void)
{
  appsFaultPolicyReset(&apps.fault_policy);
  apps.fault_latched_status 		= APPS_STATUS_OK;
  apps.fault_latched 				= false;
}

static void appsTakeIsrFaultSnapshot(apps_isr_fault_snapshot_t *p_snapshot)
{
  uint32_t primask;

  primask = __get_PRIMASK();
  __disable_irq();
  p_snapshot->observed_status = apps.isr_fault_status;
  p_snapshot->effective_status = apps.isr_effective_fault_status;
  p_snapshot->actionable_events = apps.isr_actionable_fault_events;
  apps.isr_actionable_fault_events = APPS_STATUS_OK;
  if (primask == 0U)
  {
    __enable_irq();
  }
}

static void appsPublishAdcFaultStatus(uint32_t status)
{
  uint32_t effective_status;

  effective_status = appsFaultPolicyUpdate(&apps.fault_policy, status);
  apps.isr_fault_status = status;
  apps.isr_effective_fault_status = effective_status;

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

static bool appsTryReleaseIsrOutputInhibit(void)
{
  uint32_t primask;
  bool released;

  primask = __get_PRIMASK();
  __disable_irq();

  if ((apps.isr_output_inhibit == true) &&
      (apps.isr_fault_status == APPS_STATUS_OK) &&
      (apps.isr_effective_fault_status == APPS_STATUS_OK) &&
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

/* Fail-safe status output: both APPS plausibility and the latest CAN TX must pass. */
static void appsUpdateReadyGpio(void)
{
  uint32_t primask;
  bool ready;

  /* Keep the read-and-write atomic against the ADC ISR's fail-low write. */
  primask = __get_PRIMASK();
  __disable_irq();
  ready = (apps.valid == true) &&
          (apps.can_tx_ok == true) &&
          (apps.can_tx_muted != true) &&
          (apps.isr_output_inhibit != true);

  gpioPinWrite(APPS_READY_GPIO_CH, ready);
  if (primask == 0U)
  {
    __enable_irq();
  }
}

/* Clear stale pedal/valid values after an ADC read failure. */
static void appsClearLastResult(void)
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

static uint16_t appsRawToDigital(uint8_t signal, uint16_t raw, bool *p_range_ok)
{
  int32_t raw_min 	= apps.raw_min[signal];
  int32_t raw_max 	= apps.raw_max[signal];
  int32_t span 		= raw_max - raw_min;
  int32_t scaled;

  *p_range_ok = (raw >= apps.valid_raw_min[signal]) &&
                (raw <= apps.valid_raw_max[signal]);

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

static void appsAdcCallback(void)
{
  bool signal1_range_ok 	= false;
  bool signal2_range_ok 	= false;
  uint16_t signal1_per_mille;
  uint16_t signal2_per_mille;
  uint16_t difference_per_mille;
  int32_t raw_signal1;
  int32_t raw_signal2;
  uint32_t status 			= APPS_STATUS_OK;

  /* Callback is registered only after apps.initialized is set, so it is
   * always true here; only calibration validity can still be missing. */
  if (apps.configured != true)
  {
    appsPublishAdcFaultStatus(APPS_STATUS_NOT_CONFIGURED);
    return;
  }

  raw_signal1 = adcRead(APPS_SIGNAL1_ADC_CH);
  raw_signal2 = adcRead(APPS_SIGNAL2_ADC_CH);

  if ((raw_signal1 < 0) || (raw_signal1 > UINT16_MAX) ||
      (raw_signal2 < 0) || (raw_signal2 > UINT16_MAX))
  {
    appsPublishAdcFaultStatus(APPS_STATUS_ADC_READ_FAULT);
    return;
  }

  signal1_per_mille 		= appsRawToDigital(APPS_SIGNAL1, (uint16_t)raw_signal1, &signal1_range_ok);
  signal2_per_mille 		= appsRawToDigital(APPS_SIGNAL2, (uint16_t)raw_signal2, &signal2_range_ok);

  if (signal1_range_ok != true)
  {
    status |= APPS_STATUS_SIGNAL1_RANGE_FAULT;
  }
  if (signal2_range_ok != true)
  {
    status |= APPS_STATUS_SIGNAL2_RANGE_FAULT;
  }

  if (signal1_per_mille > signal2_per_mille)
  {
    difference_per_mille = signal1_per_mille - signal2_per_mille;
  }
  else
  {
    difference_per_mille = signal2_per_mille - signal1_per_mille;
  }

  if (difference_per_mille >= APPS_DIFF_MAX)
  {
    status |= APPS_STATUS_DIFF_FAULT;
  }

  appsPublishAdcFaultStatus(status);
}

static void appsSetFaultState(uint32_t observed_status,
                              uint32_t effective_status)
{
  apps.fault_latched_status = effective_status;
  apps.fault_latched = (effective_status != APPS_STATUS_OK);
  apps.observed_status = observed_status;

  apps.status = observed_status | effective_status;
  if (apps.fault_latched == true)
  {
    apps.status |= APPS_STATUS_FAULT_LATCHED;
  }
}

/*
 * ADC failures detected by the main loop (startup/read/stale watchdog) must
 * enter the same sticky output-inhibit episode as faults detected in the ADC
 * ISR.  Otherwise an ADC failure followed immediately by a pending DIFF could
 * re-enable torque before one fully healthy sample has been observed.
 */
static void appsPublishMainImmediateFault(uint32_t observed_status)
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
  apps.fault_policy.failure_active = true;
  effective_status = apps.fault_policy.active_status;

  apps.isr_effective_fault_status = effective_status;
  apps.isr_actionable_fault_events |= immediate_status;
  apps.isr_output_inhibit = true;

  /* This is the bounded fail-low action; CAN zero is sent by the main loop. */
  gpioPinWrite(APPS_READY_GPIO_CH, false);
  appsSetFaultState(observed_status, effective_status);

  if (primask == 0U)
  {
    __enable_irq();
  }
}

static bool appsEvaluate(uint16_t raw_signal1, uint16_t raw_signal2)
{
  bool range_ok[APPS_ADC_MAX] 	= {false, false};
  uint32_t instant_status 		= APPS_STATUS_OK;
  apps_isr_fault_snapshot_t isr_snapshot;
  uint32_t effective_status;
  uint16_t difference_per_mille;

  appsTakeIsrFaultSnapshot(&isr_snapshot);

  apps.raw[APPS_SIGNAL1] 		= raw_signal1;
  apps.raw[APPS_SIGNAL2] 		= raw_signal2;

  apps.percent_per_mille[APPS_SIGNAL1] = 0U;
  apps.percent_per_mille[APPS_SIGNAL2] = 0U;

  apps.pedal_per_mille = 0U;
  apps.command_sent = 0;
  apps.adc_ok = true;
  apps.valid = false;
  apps.can_tx_ok = false;
  apps.can_tx_muted = false;

  if ((apps.initialized != true) || (apps.configured != true))
  {
    appsPublishMainImmediateFault(APPS_STATUS_NOT_CONFIGURED);
    return false;
  }

  apps.percent_per_mille[APPS_SIGNAL1] = appsRawToDigital(APPS_SIGNAL1, raw_signal1, &range_ok[APPS_SIGNAL1]);
  apps.percent_per_mille[APPS_SIGNAL2] = appsRawToDigital(APPS_SIGNAL2, raw_signal2, &range_ok[APPS_SIGNAL2]);

  if (range_ok[APPS_SIGNAL1] != true)
  {
    instant_status |= APPS_STATUS_SIGNAL1_RANGE_FAULT;
  }
  if (range_ok[APPS_SIGNAL2] != true)
  {
    instant_status |= APPS_STATUS_SIGNAL2_RANGE_FAULT;
  }

  if (apps.percent_per_mille[APPS_SIGNAL1] > apps.percent_per_mille[APPS_SIGNAL2])
  {
    difference_per_mille = apps.percent_per_mille[APPS_SIGNAL1] - apps.percent_per_mille[APPS_SIGNAL2];
  }
  else
  {
    difference_per_mille = apps.percent_per_mille[APPS_SIGNAL2] - apps.percent_per_mille[APPS_SIGNAL1];
  }

  if (difference_per_mille >= APPS_DIFF_MAX)
  {
    instant_status |= APPS_STATUS_DIFF_FAULT;
  }

  instant_status |= isr_snapshot.observed_status;
  instant_status |= isr_snapshot.actionable_events;

  apps.pedal_per_mille = (uint16_t)(((uint32_t)apps.percent_per_mille[APPS_SIGNAL1] +
                		 (uint32_t)apps.percent_per_mille[APPS_SIGNAL2]) / APPS_ADC_MAX);

  effective_status = isr_snapshot.effective_status |
                     isr_snapshot.actionable_events |
                     (instant_status & APPS_IMMEDIATE_FAULT_MASK);
  appsSetFaultState(instant_status, effective_status);

  if ((effective_status == APPS_STATUS_OK) &&
      (appsTryReleaseIsrOutputInhibit() == true))
  {
    apps.valid = true;
  }

  return true;
}


bool appsSetConfig(uint16_t signal1_raw_min,
                   uint16_t signal1_raw_max,
                   uint16_t signal2_raw_min,
                   uint16_t signal2_raw_max)
{
  uint32_t primask;
  uint32_t observed_faults;

  /*
   * Runtime calibration must never become a fault-clear back door.  Hold the
   * ADC callback off while checking and replacing the four limits so it can
   * neither observe a partially updated configuration nor lose an active
   * APPS fault episode.
   */
  primask = __get_PRIMASK();
  __disable_irq();
  observed_faults = (apps.observed_status |
                     apps.isr_fault_status |
                     apps.isr_effective_fault_status |
                     apps.isr_actionable_fault_events) &
                    (APPS_IMMEDIATE_FAULT_MASK | APPS_TIMED_FAULT_MASK);
  if ((observed_faults != APPS_STATUS_OK) ||
      (apps.fault_policy.timer_active == true) ||
      (apps.isr_output_inhibit == true))
  {
    if (primask == 0U)
    {
      __enable_irq();
    }
    return false;
  }

  apps.raw_min[APPS_SIGNAL1] = signal1_raw_min;
  apps.raw_max[APPS_SIGNAL1] = signal1_raw_max;
  apps.raw_min[APPS_SIGNAL2] = signal2_raw_min;
  apps.raw_max[APPS_SIGNAL2] = signal2_raw_max;

  apps.configured = appsCalibrationIsValid();
  appsResetFaultState();

  if (primask == 0U)
  {
    __enable_irq();
  }

  return apps.configured;
}

static void appsResetFaultState(void)
{
  uint32_t primask;

  primask = __get_PRIMASK();
  __disable_irq();
  appsClearFaultTimer();
  apps.isr_fault_status = APPS_STATUS_OK;
  apps.isr_effective_fault_status = APPS_STATUS_OK;
  apps.isr_actionable_fault_events = APPS_STATUS_OK;
  apps.isr_output_inhibit = false;
  if (primask == 0U)
  {
    __enable_irq();
  }

  apps.observed_status = APPS_STATUS_OK;
  apps.valid = false;
  apps.command_sent = 0;
  apps.can_tx_ok = false;
  apps.can_tx_muted = false;
  apps.status =
      (apps.configured == true) ? APPS_STATUS_OK : APPS_STATUS_NOT_CONFIGURED;
  appsUpdateReadyGpio();
}

static bool appsTryClearFault(void)
{
  uint32_t primask;
  uint32_t observed_faults;

  primask = __get_PRIMASK();
  __disable_irq();
  observed_faults = (apps.observed_status |
                     apps.isr_fault_status |
                     apps.isr_effective_fault_status |
                     apps.isr_actionable_fault_events) &
                    (APPS_IMMEDIATE_FAULT_MASK | APPS_TIMED_FAULT_MASK);
  if ((observed_faults != APPS_STATUS_OK) ||
      (apps.fault_policy.timer_active == true) ||
      (apps.isr_output_inhibit == true))
  {
    if (primask == 0U)
    {
      __enable_irq();
    }
    return false;
  }

  appsResetFaultState();
  if (primask == 0U)
  {
    __enable_irq();
  }
  return true;
}

void appsClearFault(void)
{
  (void)appsTryClearFault();
}

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
  p_snapshot->fault_latched 					= apps.fault_latched;
  p_snapshot->configured 						= apps.configured;

  return true;
}

static bool appsUpdate(void)
{
  int32_t raw_signal1;
  int32_t raw_signal2;
  uint32_t adc_sequence;
  uint32_t now_ms;

  if (apps.initialized != true)     return false;

  if (adcUpdate() != true)
  {
    uint32_t status = APPS_STATUS_ADC_READ_FAULT;

    if (apps.configured != true)
    {
      status |= APPS_STATUS_NOT_CONFIGURED;
    }

    appsClearLastResult();
    appsPublishMainImmediateFault(status);

    return false;
  }

  now_ms = millis();
  adc_sequence = apps.isr_adc_sequence;
  if (adc_sequence == apps.adc_sequence_seen)
  {
    if ((uint32_t)(now_ms - apps.adc_sequence_seen_ms) >= APPS_ADC_STALE_MS)
    {
      appsClearLastResult();
      appsPublishMainImmediateFault(APPS_STATUS_ADC_READ_FAULT);
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

  if ((raw_signal1 < 0) || (raw_signal1 > UINT16_MAX) ||
      (raw_signal2 < 0) || (raw_signal2 > UINT16_MAX))
  {
    appsClearLastResult();
    appsPublishMainImmediateFault(APPS_STATUS_ADC_READ_FAULT);
    return false;
  }

  return appsEvaluate((uint16_t)raw_signal1, (uint16_t)raw_signal2);
}

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
  apps.cutoff = APPS_CUTOFF_NONE;
  if ((command_to_send > 0) && (command_to_send <= APPS_TORQUE_CUTOFF))
  {
    command_to_send = 0;
    apps.cutoff = APPS_CUTOFF_LOW;
  }
  else if (command_to_send >= APPS_TORQUE_CUTOFF_HIGH)
  {
    command_to_send = APPS_TORQUE_MAX;
    apps.cutoff = APPS_CUTOFF_HIGH;
  }

  /* Close the evaluate-to-send race if the ADC ISR inhibits in between. */
  if (apps.isr_output_inhibit == true)
  {
    command_to_send = 0;
    apps.valid = false;
    apps.cutoff = APPS_CUTOFF_NONE;
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

static int16_t appsMakePedalCommand(void)
{
  uint32_t command;

  command = ((uint32_t)apps.pedal_per_mille *
             (uint32_t)APPS_TORQUE_MAX) /
            (uint32_t)APPS_PER_MILLE_MAX;

  if (command > (uint32_t)INT16_MAX)
  {
    command = (uint32_t)INT16_MAX;
  }

  return (int16_t)command;
}

bool appsRunPedalCommand(void)
{
  bool adc_result             = appsUpdate();
  int16_t pedal_command       = appsMakePedalCommand();
  bool can_result             = appsSendCommand(pedal_command);

  appsUpdateReadyGpio();

  return adc_result && can_result;
}

#ifdef _USE_HW_CLI

/* Absolute APPS1/APPS2 gap in per-mille, the value compared to APPS_DIFF_MAX. */
static uint16_t appsCliDiffPerMille(void)
{
  if (apps.percent_per_mille[APPS_SIGNAL1] > apps.percent_per_mille[APPS_SIGNAL2])
  {
    return apps.percent_per_mille[APPS_SIGNAL1] -
           apps.percent_per_mille[APPS_SIGNAL2];
  }

  return apps.percent_per_mille[APPS_SIGNAL2] -
         apps.percent_per_mille[APPS_SIGNAL1];
}

/*
 * One word for the whole plausibility state machine, shared by status,
 * monitor and verify so the three views never disagree.
 */
static const char *appsCliStateName(void)
{
  if (apps.configured != true)
  {
    return "NOCFG";
  }
  if (apps.fault_latched == true)
  {
    return "FAULT";
  }
  if (apps.fault_policy.timer_active == true)
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
  if (apps.fault_policy.elapsed_ms > APPS_FAULT_MS)
  {
    return APPS_FAULT_MS;
  }

  return apps.fault_policy.elapsed_ms;
}

static const char *appsCliDiffStateName(void)
{
  if ((apps.observed_status & APPS_STATUS_DIFF_FAULT) == 0U)
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
    input_status         = apps.observed_status;
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
    cliPrintf("FAILURE      : %s\n", apps.fault_latched ? "ACTIVE" : "NONE");
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
    bool     seen_diff_pending     = false;
    bool     seen_diff_confirmed   = false;
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
    cliPrintf("Diff rule limit : %lu ms\n", (unsigned long)APPS_FAULT_MS);
    cliPrintf("ISR confirm     : %lu samples @ %lu Hz\n",
              (unsigned long)APPS_DIFF_CONFIRM_SAMPLES,
              (unsigned long)ADC_SAMPLE_HZ);
    cliPrintf("Range/ADC fault : immediate\n");
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

      if (((seen_diff_confirmed == true) || (seen_immediate == true)) &&
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

      if ((apps.fault_policy.timer_active == true) && (prev_pending != true))
      {
        seen_diff_pending = true;

        cliPrintf("[%06lu ms] DIFF >= LIMIT DETECTED\n",
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
        cliPrintf("[%06lu ms] DIFF TIMER START\n\n", (unsigned long)now_ms);
      }

      if ((immediate_status != APPS_STATUS_OK) &&
          ((prev_effective_status & APPS_IMMEDIATE_FAULT_MASK) == APPS_STATUS_OK))
      {
        seen_immediate = true;
        new_actionable_fault = true;

        cliPrintf("[%06lu ms] IMMEDIATE RANGE/ADC FAILURE\n",
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

      /* DIFF alone controls the 100 ms timer and its recovery. */
      if ((apps.fault_policy.timer_active != true) && (prev_pending == true))
      {
        cliPrintf("[%06lu ms] DIFF RETURNED WITHIN LIMIT\n",
                  (unsigned long)now_ms);
        cliPrintf("            S1=%u.%u%%\n",
                  apps.percent_per_mille[APPS_SIGNAL1] / 10U,
                  apps.percent_per_mille[APPS_SIGNAL1] % 10U);
        cliPrintf("            S2=%u.%u%%\n",
                  apps.percent_per_mille[APPS_SIGNAL2] / 10U,
                  apps.percent_per_mille[APPS_SIGNAL2] % 10U);
        cliPrintf("            DIFF=%u.%u%%\n",
                  difference_per_mille / 10U, difference_per_mille % 10U);

        if ((prev_effective_status & APPS_STATUS_DIFF_FAULT) == 0U)
        {
          cliPrintf("[%06lu ms] DIFF TIMER RESET (%lu / %lu ms)\n",
                    (unsigned long)now_ms,
                    (unsigned long)pending_ms,
                    (unsigned long)APPS_FAULT_MS);
          cliPrintf("            DIFF FAILURE NOT CONFIRMED\n\n");
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
        cliPrintf("[%06lu ms] RANGE/ADC FAILURE CLEARED\n",
                  (unsigned long)now_ms);
        if ((effective_status != APPS_STATUS_OK) || (apps.valid != true))
        {
          cliPrintf("            OUTPUT INHIBIT HELD: APPS FAULT EPISODE ACTIVE\n");
        }
      }

      if (apps.fault_policy.timer_active == true)
      {
        pending_ms = appsCliFaultTimerMs();
      }
      prev_pending = apps.fault_policy.timer_active;
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
    cliPrintf("Immediate RANGE/ADC failure  : %s\n",
              seen_immediate ? "PASS" : "NOT SEEN");
    cliPrintf("DIFF pending detected        : %s\n",
              seen_diff_pending ? "PASS" : "NOT SEEN");
    cliPrintf("DIFF confirmed >= %lu ms     : %s\n",
              (unsigned long)APPS_FAULT_MS,
              (seen_diff_pending != true) ? "NOT SEEN" :
              (seen_diff_confirmed ? "PASS" : "FAIL"));
    cliPrintf("Torque command inhibited     : %s\n",
              seen_torque_zero ? "PASS" : "FAIL");
    cliPrintf("READY GPIO LOW               : %s\n",
              seen_ready_low ? "PASS" : "FAIL");
    cliPrintf("CAN transmission             : %s\n",
              can_ok ? "PASS" : "FAIL");
    cliPrintf("Recovered after fault clears : %s\n",
              ((seen_immediate != true) && (seen_diff_confirmed != true)) ?
              "NOT TESTED" : (seen_recovered ? "PASS" : "NOT OBSERVED"));
    cliPrintf("\n");

    if ((seen_immediate != true) && (seen_diff_pending != true))
    {
      cliPrintf("RESULT                       : NO FAULT INJECTED\n");
    }
    else if (((seen_immediate == true) || (seen_diff_confirmed == true)) &&
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
    cliPrintf("configured  : %s\n",
              apps.configured ? "true" : "false");
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
      else if (apps.cutoff == APPS_CUTOFF_LOW)
      {
        state = "CUT";
      }
      else if (apps.cutoff == APPS_CUTOFF_HIGH)
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
      if (apps.cutoff == APPS_CUTOFF_LOW)
      {
        cutoff_count++;
      }
      if (apps.cutoff == APPS_CUTOFF_HIGH)
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

  if ((args->argc == 1) && args->isStr(0, "clear"))
  {
    if (appsTryClearFault() == true)
    {
      cliPrintf("apps fault policy reset\n");
    }
    else
    {
      cliPrintf("apps clear rejected: APPS fault is still active\n");
    }
    return;
  }

  cliPrintf("apps status                     : print current state once\n");
  cliPrintf("apps monitor                    : live state, %ums period\n",
            APPS_MONITOR_PERIOD_MS);
  cliPrintf("apps verify                     : plausibility verification mode\n");
  cliPrintf("apps trace                      : pedal / torque cutoff trace\n");
  cliPrintf("apps clear                      : reset current fault policy\n");
  cliPrintf("apps config                     : print thresholds and ADC setup\n");
  cliPrintf("apps help                       : this menu\n");
}

#endif
