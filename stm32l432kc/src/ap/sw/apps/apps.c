/*
 * apps.c
 */

#include "apps.h"
#include "gpio.h"

#ifdef _USE_HW_CLI
#include "cli.h"
#endif


#define APPS_TIMED_FAULT_MASK  (APPS_STATUS_ADC_READ_FAULT      | \
                                APPS_STATUS_SIGNAL1_RANGE_FAULT | \
                                APPS_STATUS_SIGNAL2_RANGE_FAULT | \
                                APPS_STATUS_DIFF_FAULT)

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
  volatile bool     isr_fault_active;

  uint32_t fault_started_ms;
  uint32_t fault_elapsed_ms;
  uint32_t fault_pending_status;
  uint32_t fault_latched_status;
  bool     fault_timer_active;
  bool     fault_latched;

  bool     initialized;
  bool     configured;
} apps_t;

static apps_t apps;

/* appsInit()이 정의보다 앞에서 호출하는 내부 함수들. */
static bool appsCalibrationIsValid(void);
static void appsClearFaultTimer(void);
static void appsAdcCallback(void);
static void appsUpdateReadyGpio(void);

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
  apps.fault_started_ms 			= 0U;
  apps.fault_elapsed_ms 			= 0U;
  apps.fault_pending_status 		= APPS_STATUS_OK;
  apps.fault_latched_status 		= APPS_STATUS_OK;
  apps.fault_timer_active 			= false;
  apps.fault_latched 				= false;
}

/* Fail-safe status output: both APPS plausibility and the latest CAN TX must pass. */
static void appsUpdateReadyGpio(void)
{
  bool ready = (apps.valid == true) &&
               (apps.can_tx_ok == true) &&
               (apps.can_tx_muted != true) &&
               (apps.status == APPS_STATUS_OK);

  gpioPinWrite(APPS_READY_GPIO_CH, ready);
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
    apps.isr_fault_status = APPS_STATUS_NOT_CONFIGURED;
    apps.isr_fault_active = true;
    return;
  }

  raw_signal1 = adcRead(APPS_SIGNAL1_ADC_CH);
  raw_signal2 = adcRead(APPS_SIGNAL2_ADC_CH);

  if ((raw_signal1 < 0) || (raw_signal1 > UINT16_MAX) ||
      (raw_signal2 < 0) || (raw_signal2 > UINT16_MAX))
  {
    apps.isr_fault_status = APPS_STATUS_ADC_READ_FAULT;
    apps.isr_fault_active = true;
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

  apps.isr_fault_status = status;
  apps.isr_fault_active = (status != APPS_STATUS_OK);
}

static void appsUpdateFaultTimer(uint32_t instant_status)
{
  uint32_t timed_fault = instant_status & APPS_TIMED_FAULT_MASK;

  if (timed_fault == APPS_STATUS_OK)
  {
    apps.fault_timer_active = false;
    apps.fault_started_ms = 0U;
    apps.fault_elapsed_ms = 0U;
    apps.fault_pending_status = APPS_STATUS_OK;
    apps.fault_latched_status = APPS_STATUS_OK;
    apps.fault_latched = false;
  }
  else
  {
    uint32_t now_ms = millis();

    if ((apps.fault_timer_active != true) ||
        (apps.fault_pending_status != timed_fault))
    {
      apps.fault_timer_active = true;
      apps.fault_started_ms = now_ms;
      apps.fault_elapsed_ms = 0U;
      apps.fault_pending_status = timed_fault;
      apps.fault_latched_status = APPS_STATUS_OK;
      apps.fault_latched = false;
    }
    else
    {
      apps.fault_elapsed_ms = now_ms - apps.fault_started_ms;

      if (apps.fault_elapsed_ms >= APPS_FAULT_MS)
      {
        apps.fault_latched = true;
        apps.fault_latched_status = timed_fault;
      }
    }
  }

  apps.status = instant_status | apps.fault_latched_status;
  if (apps.fault_latched == true)
  {
    apps.status |= APPS_STATUS_FAULT_LATCHED;
  }
}

static bool appsEvaluate(uint16_t raw_signal1, uint16_t raw_signal2)
{
  bool range_ok[APPS_ADC_MAX] 	= {false, false};
  uint32_t instant_status 		= APPS_STATUS_OK;
  uint32_t isr_fault_status 	= apps.isr_fault_status;
  bool 		isr_fault_active 	= apps.isr_fault_active;
  uint16_t difference_per_mille;

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
    appsUpdateFaultTimer(APPS_STATUS_NOT_CONFIGURED);
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

  if (isr_fault_active == true)
  {
    instant_status |= isr_fault_status;
  }

  apps.pedal_per_mille = (uint16_t)(((uint32_t)apps.percent_per_mille[APPS_SIGNAL1] +
                		 (uint32_t)apps.percent_per_mille[APPS_SIGNAL2]) / APPS_ADC_MAX);

  appsUpdateFaultTimer(instant_status);

  if ((instant_status == APPS_STATUS_OK) && (apps.fault_latched != true))
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
  apps.raw_min[APPS_SIGNAL1] = signal1_raw_min;
  apps.raw_max[APPS_SIGNAL1] = signal1_raw_max;
  apps.raw_min[APPS_SIGNAL2] = signal2_raw_min;
  apps.raw_max[APPS_SIGNAL2] = signal2_raw_max;

  apps.configured = appsCalibrationIsValid();
  appsClearFault();

  return apps.configured;
}

void appsClearFault(void)
{
  appsClearFaultTimer();
  apps.isr_fault_status = APPS_STATUS_OK;
  apps.isr_fault_active = false;
  apps.valid = false;
  apps.command_sent = 0;
  apps.can_tx_ok = false;
  apps.can_tx_muted = false;
  apps.status =
      (apps.configured == true) ? APPS_STATUS_OK : APPS_STATUS_NOT_CONFIGURED;
  appsUpdateReadyGpio();
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

  if (apps.initialized != true)     return false;

  if (adcUpdate() != true)
  {
    uint32_t status = APPS_STATUS_ADC_READ_FAULT;

    if (apps.configured != true)
    {
      status |= APPS_STATUS_NOT_CONFIGURED;
    }

    appsClearLastResult();
    appsUpdateFaultTimer(status);

    return false;
  }

  raw_signal1 = adcRead(APPS_SIGNAL1_ADC_CH);
  raw_signal2 = adcRead(APPS_SIGNAL2_ADC_CH);

  if ((raw_signal1 < 0) || (raw_signal1 > UINT16_MAX) ||
      (raw_signal2 < 0) || (raw_signal2 > UINT16_MAX))
  {
    appsClearLastResult();
    appsUpdateFaultTimer(APPS_STATUS_ADC_READ_FAULT);
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

  if (apps.valid != true)
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
  if (apps.fault_timer_active == true)
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
  if (apps.fault_elapsed_ms > APPS_FAULT_MS)
  {
    return APPS_FAULT_MS;
  }

  return apps.fault_elapsed_ms;
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
    uint16_t difference_per_mille;
    uint16_t signal1_mv;
    uint16_t signal2_mv;

    (void)appsRunPedalCommand();

    status               = apps.status;
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
              ((status & APPS_STATUS_SIGNAL1_RANGE_FAULT) != 0U) ? "RANGE" : "OK");
    cliPrintf("SIGNAL2      : raw=%-5u %u.%03uV %5u.%u%%   [%s]\n",
              apps.raw[APPS_SIGNAL2],
              signal2_mv / 1000U, signal2_mv % 1000U,
              apps.percent_per_mille[APPS_SIGNAL2] / 10U,
              apps.percent_per_mille[APPS_SIGNAL2] % 10U,
              ((status & APPS_STATUS_SIGNAL2_RANGE_FAULT) != 0U) ? "RANGE" : "OK");
    cliPrintf("\n");
    cliPrintf("DIFFERENCE   : %u.%u%% / %u.%u%%      [%s]\n",
              difference_per_mille / 10U, difference_per_mille % 10U,
              APPS_DIFF_MAX / 10U, APPS_DIFF_MAX % 10U,
              ((status & APPS_STATUS_DIFF_FAULT) != 0U) ? "FAULT" : "OK");
    cliPrintf("\n");
    cliPrintf("FAULT        : ");
    appsCliPrintFaultNames(status, "               ");
    cliPrintf("FAULT TIMER  : %lu / %lu ms\n",
              (unsigned long)appsCliFaultTimerMs(),
              (unsigned long)APPS_FAULT_MS);
    cliPrintf("LATCHED      : %s\n", apps.fault_latched ? "YES" : "NO");
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

  /*
   * Field demonstration mode. Runs the real control loop and prints one time
   * stamped block per state change, so a shorted APPS line can be shown to
   * produce fault -> confirm -> latch -> torque inhibit -> READY low in order.
   */
  if ((args->argc == 1) && args->isStr(0, "verify"))
  {
    uint32_t start_ms;
    bool     first_sample     = true;
    bool     prev_pending     = false;
    bool     prev_latched     = false;
    bool     seen_detected    = false;
    bool     seen_confirmed   = false;
    bool     seen_torque_zero = false;
    bool     seen_ready_low   = false;
    bool     seen_recovered   = false;
    bool     can_ok           = true;
    uint32_t pending_ms       = 0U;

    cliPrintf("========================================\n");
    cliPrintf(" APPS PLAUSIBILITY VERIFICATION\n");
    cliPrintf("========================================\n");
    cliPrintf("Diff limit      : %u.%u %%\n",
              APPS_DIFF_MAX / 10U, APPS_DIFF_MAX % 10U);
    cliPrintf("Fault confirm   : %lu ms\n", (unsigned long)APPS_FAULT_MS);
    cliPrintf("CAN channel     : %u\n", APPS_CAN_CH);
    cliPrintf("\n");
    cliPrintf("Short APPS signal to begin test.\n");
    cliPrintf("Press any key to finish.\n\n");

    /* Started after the banner so the log begins at 0 ms, not at the
       time the header took to go out over the UART. */
    start_ms = millis();

    while (cliKeepLoop() == true)
    {
      uint32_t now_ms;
      uint32_t status;
      uint16_t difference_per_mille;

      (void)appsRunPedalCommand();

      now_ms               = millis() - start_ms;
      status               = apps.status;
      difference_per_mille = appsCliDiffPerMille();

      if ((apps.can_tx_muted != true) && (apps.can_tx_ok != true))
      {
        can_ok = false;
      }

      /*
       * A confirmed fault must release itself once the signals agree again:
       * the rule asks for the drive to stay shut down while the implausibility
       * is present, not until the car is power cycled.
       */
      if ((seen_confirmed == true) && (apps.valid == true) &&
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

      if ((apps.fault_timer_active == true) && (prev_pending != true))
      {
        seen_detected = true;

        cliPrintf("[%06lu ms] PLAUSIBILITY FAULT DETECTED\n",
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
                  ((status & APPS_STATUS_DIFF_FAULT) != 0U) ? " [FAULT]" : "");
        cliPrintf("[%06lu ms] FAULT TIMER START\n\n", (unsigned long)now_ms);
      }

      if ((apps.fault_latched == true) && (prev_latched != true))
      {
        seen_confirmed = true;

        cliPrintf("[%06lu ms] FAULT CONFIRMED\n", (unsigned long)now_ms);
        cliPrintf("            elapsed=%lu ms\n",
                  (unsigned long)appsCliFaultTimerMs());
        cliPrintf("[%06lu ms] FAULT LATCHED\n", (unsigned long)now_ms);
        cliPrintf("            ");
        appsCliPrintFaultNames(apps.fault_latched_status, "            ");

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

      /*
       * Input came back inside the limits. Below the confirm time the timer
       * just restarts, so nothing latches; past it the latch decides whether
       * torque stays inhibited.
       */
      if ((apps.fault_timer_active != true) && (prev_pending == true))
      {
        cliPrintf("[%06lu ms] INPUT RETURNED PLAUSIBLE\n",
                  (unsigned long)now_ms);
        cliPrintf("            S1=%u.%u%%\n",
                  apps.percent_per_mille[APPS_SIGNAL1] / 10U,
                  apps.percent_per_mille[APPS_SIGNAL1] % 10U);
        cliPrintf("            S2=%u.%u%%\n",
                  apps.percent_per_mille[APPS_SIGNAL2] / 10U,
                  apps.percent_per_mille[APPS_SIGNAL2] % 10U);
        cliPrintf("            DIFF=%u.%u%%\n",
                  difference_per_mille / 10U, difference_per_mille % 10U);

        if (prev_latched != true)
        {
          cliPrintf("[%06lu ms] FAULT TIMER RESET (%lu / %lu ms)\n",
                    (unsigned long)now_ms,
                    (unsigned long)pending_ms,
                    (unsigned long)APPS_FAULT_MS);
          cliPrintf("            FAULT NOT LATCHED\n\n");
        }
        else
        {
          cliPrintf("[%06lu ms] FAULT CLEARED\n", (unsigned long)now_ms);
          cliPrintf("[%06lu ms] TORQUE RE-ENABLED\n", (unsigned long)now_ms);
          cliPrintf("            command=%d\n", apps.command_sent);
          cliPrintf("[%06lu ms] READY GPIO -> %s\n\n",
                    (unsigned long)now_ms,
                    gpioPinRead(APPS_READY_GPIO_CH) ? "HIGH" : "LOW");
        }
      }

      if (apps.fault_timer_active == true)
      {
        pending_ms = appsCliFaultTimerMs();
      }
      prev_pending = apps.fault_timer_active;
      prev_latched = apps.fault_latched;

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
    cliPrintf("Plausibility fault detected  : %s\n",
              seen_detected ? "PASS" : "FAIL");
    cliPrintf("Fault confirmed >= %lu ms    : %s\n",
              (unsigned long)APPS_FAULT_MS,
              seen_confirmed ? "PASS" : "FAIL");
    cliPrintf("Torque command inhibited     : %s\n",
              seen_torque_zero ? "PASS" : "FAIL");
    cliPrintf("READY GPIO LOW               : %s\n",
              seen_ready_low ? "PASS" : "FAIL");
    cliPrintf("CAN transmission             : %s\n",
              can_ok ? "PASS" : "FAIL");
    cliPrintf("Recovered when plausible     : %s\n",
              (seen_confirmed != true) ? "NOT TESTED"
                                       : (seen_recovered ? "PASS" : "FAIL"));
    cliPrintf("\n");

    if (seen_detected != true)
    {
      cliPrintf("RESULT                       : NO FAULT INJECTED\n");
    }
    else if ((seen_confirmed == true) &&
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
    cliPrintf("fault confirm : %lums\n", (unsigned long)APPS_FAULT_MS);
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
    appsClearFault();
    cliPrintf("apps fault clear\n");
    return;
  }

  cliPrintf("apps status                     : print current state once\n");
  cliPrintf("apps monitor                    : live state, %ums period\n",
            APPS_MONITOR_PERIOD_MS);
  cliPrintf("apps verify                     : plausibility verification mode\n");
  cliPrintf("apps trace                      : pedal / torque cutoff trace\n");
  cliPrintf("apps clear                      : clear latched fault\n");
  cliPrintf("apps config                     : print thresholds and ADC setup\n");
  cliPrintf("apps help                       : this menu\n");
}

#endif
