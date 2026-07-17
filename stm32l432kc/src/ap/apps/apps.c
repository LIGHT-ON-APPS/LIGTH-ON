/*
 * apps.c
 */

#include "apps.h"

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

typedef struct
{
  uint16_t raw_min[APPS_ADC_MAX];
  uint16_t raw_max[APPS_ADC_MAX];
  uint16_t raw[APPS_ADC_MAX];
  uint16_t voltage_mv[APPS_ADC_MAX];
  uint16_t percent_per_mille[APPS_ADC_MAX];
  uint16_t difference_per_mille;
  uint16_t pedal_per_mille;
  int16_t  command_sent;
  uint32_t status;
  bool     adc_ok;
  bool     valid;
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

#ifdef _USE_HW_CLI
static bool apps_command_registered = false;

static void appsCli(cli_args_t *args);
#endif


/* Calibration is invalid if any channel has the same min and max. */
static bool appsCalibrationIsValid(void)
{
  for (uint8_t i = 0; i < APPS_ADC_MAX; i++)
  {
    if (apps.raw_min[i] == apps.raw_max[i])
    {
      return false;
    }
  }

  return true;
}

static void appsClearFaultTimer(void)
{
  apps.fault_started_ms = 0U;
  apps.fault_elapsed_ms = 0U;
  apps.fault_pending_status = APPS_STATUS_OK;
  apps.fault_latched_status = APPS_STATUS_OK;
  apps.fault_timer_active = false;
  apps.fault_latched = false;
}

/* Clear stale pedal/valid values after an ADC read failure. */
static void appsClearLastResult(void)
{
  memset(apps.raw, 0, sizeof(apps.raw));
  memset(apps.voltage_mv, 0, sizeof(apps.voltage_mv));
  memset(apps.percent_per_mille, 0, sizeof(apps.percent_per_mille));

  apps.difference_per_mille = 0U;
  apps.pedal_per_mille = 0U;
  apps.command_sent = 0;
  apps.adc_ok = false;
  apps.valid = false;
  apps.can_tx_ok = false;
  apps.can_tx_muted = false;
}

static uint16_t appsRawToPerMille(uint8_t signal, uint16_t raw, bool *p_range_ok)
{
  int32_t raw_min = apps.raw_min[signal];
  int32_t raw_max = apps.raw_max[signal];
  int32_t raw_low = (raw_min < raw_max) ? raw_min : raw_max;
  int32_t raw_high = (raw_min > raw_max) ? raw_min : raw_max;
  int32_t span = raw_max - raw_min;
  int32_t scaled;

  *p_range_ok = true;

  if ((int32_t)raw < (raw_low - (int32_t)APPS_RAW_LOW_TOL))
  {
    *p_range_ok = false;
  }
  if ((int32_t)raw > (raw_high + (int32_t)APPS_RAW_HIGH_TOL))
  {
    *p_range_ok = false;
  }

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
  bool signal1_range_ok = false;
  bool signal2_range_ok = false;
  uint16_t signal1_per_mille;
  uint16_t signal2_per_mille;
  uint16_t difference_per_mille;
  int32_t raw_signal1;
  int32_t raw_signal2;
  uint32_t status = APPS_STATUS_OK;

  if ((apps.initialized != true) || (apps.configured != true))
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

  signal1_per_mille =
      appsRawToPerMille(APPS_SIGNAL1,
                        (uint16_t)raw_signal1,
                        &signal1_range_ok);
  signal2_per_mille =
      appsRawToPerMille(APPS_SIGNAL2,
                        (uint16_t)raw_signal2,
                        &signal2_range_ok);

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

static bool appsEvaluateRawPair(uint16_t raw_signal1, uint16_t raw_signal2)
{
  bool range_ok[APPS_ADC_MAX] = {false, false};
  uint32_t instant_status = APPS_STATUS_OK;
  uint32_t isr_fault_status = apps.isr_fault_status;
  bool isr_fault_active = apps.isr_fault_active;

  apps.raw[APPS_SIGNAL1] = raw_signal1;
  apps.raw[APPS_SIGNAL2] = raw_signal2;
  apps.voltage_mv[APPS_SIGNAL1] = (uint16_t)adcConvMillivolts(APPS_SIGNAL1_ADC_CH, raw_signal1);
  apps.voltage_mv[APPS_SIGNAL2] = (uint16_t)adcConvMillivolts(APPS_SIGNAL2_ADC_CH, raw_signal2);

  apps.percent_per_mille[APPS_SIGNAL1] = 0U;
  apps.percent_per_mille[APPS_SIGNAL2] = 0U;
  apps.difference_per_mille = 0U;
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

  apps.percent_per_mille[APPS_SIGNAL1] =
      appsRawToPerMille(APPS_SIGNAL1, raw_signal1, &range_ok[APPS_SIGNAL1]);
  apps.percent_per_mille[APPS_SIGNAL2] =
      appsRawToPerMille(APPS_SIGNAL2, raw_signal2, &range_ok[APPS_SIGNAL2]);

  if (range_ok[APPS_SIGNAL1] != true)
  {
    instant_status |= APPS_STATUS_SIGNAL1_RANGE_FAULT;
  }
  if (range_ok[APPS_SIGNAL2] != true)
  {
    instant_status |= APPS_STATUS_SIGNAL2_RANGE_FAULT;
  }

  if (apps.percent_per_mille[APPS_SIGNAL1] >
      apps.percent_per_mille[APPS_SIGNAL2])
  {
    apps.difference_per_mille =
        apps.percent_per_mille[APPS_SIGNAL1] -
        apps.percent_per_mille[APPS_SIGNAL2];
  }
  else
  {
    apps.difference_per_mille =
        apps.percent_per_mille[APPS_SIGNAL2] -
        apps.percent_per_mille[APPS_SIGNAL1];
  }

  if (apps.difference_per_mille >= APPS_DIFF_MAX)
  {
    instant_status |= APPS_STATUS_DIFF_FAULT;
  }

  if (isr_fault_active == true)
  {
    instant_status |= isr_fault_status;
  }

  apps.pedal_per_mille =
      (uint16_t)(((uint32_t)apps.percent_per_mille[APPS_SIGNAL1] +
                  (uint32_t)apps.percent_per_mille[APPS_SIGNAL2]) /
                 APPS_ADC_MAX);

  appsUpdateFaultTimer(instant_status);

  if ((instant_status == APPS_STATUS_OK) &&
      (apps.fault_latched != true))
  {
    apps.valid = true;
  }

  return true;
}


bool appsInit(void)
{
  memset(&apps, 0, sizeof(apps));
  appsClearFaultTimer();

  apps.raw_min[APPS_SIGNAL1] = APPS_SIGNAL1_RAW_MIN_DEFAULT;
  apps.raw_max[APPS_SIGNAL1] = APPS_SIGNAL1_RAW_MAX_DEFAULT;
  apps.raw_min[APPS_SIGNAL2] = APPS_SIGNAL2_RAW_MIN_DEFAULT;
  apps.raw_max[APPS_SIGNAL2] = APPS_SIGNAL2_RAW_MAX_DEFAULT;
  apps.configured = appsCalibrationIsValid();

  apps.initialized = true;
  adcSetUpdateCallback(appsAdcCallback);
  apps.status =
      (apps.configured == true) ? APPS_STATUS_OK : APPS_STATUS_NOT_CONFIGURED;

#ifdef _USE_HW_CLI
  if (apps_command_registered != true)
  {
    apps_command_registered = cliAdd("apps", appsCli);
  }
#endif

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

  return appsEvaluateRawPair((uint16_t)raw_signal1, (uint16_t)raw_signal2);
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

  return adc_result && can_result;
}

#ifdef _USE_HW_CLI

static void appsCli(cli_args_t *args)
{
  if ((args->argc == 1) &&
      ((args->isStr(0, "info")) || (args->isStr(0, "show"))))
  {
    bool show = args->isStr(0, "show");

    while (1)
    {
      uint32_t status;
      uint16_t signal1_voltage_mv;
      uint16_t signal2_voltage_mv;

      if ((show == true) && (cliKeepLoop() != true))
      {
        break;
      }

      (void)appsRunPedalCommand();

      signal1_voltage_mv = apps.voltage_mv[APPS_SIGNAL1];
      signal2_voltage_mv = apps.voltage_mv[APPS_SIGNAL2];
      status = apps.status;

      cliPrintf("signal1 : raw=%u, %u.%03uV, %u.%u%%\n",
                apps.raw[APPS_SIGNAL1],
                signal1_voltage_mv / 1000U,
                signal1_voltage_mv % 1000U,
                apps.percent_per_mille[APPS_SIGNAL1] / 10U,
                apps.percent_per_mille[APPS_SIGNAL1] % 10U);
      cliPrintf("signal2 : raw=%u, %u.%03uV, %u.%u%%\n",
                apps.raw[APPS_SIGNAL2],
                signal2_voltage_mv / 1000U,
                signal2_voltage_mv % 1000U,
                apps.percent_per_mille[APPS_SIGNAL2] / 10U,
                apps.percent_per_mille[APPS_SIGNAL2] % 10U);
      cliPrintf("difference : %u.%u%%\n",
                apps.difference_per_mille / 10U,
                apps.difference_per_mille % 10U);
      cliPrintf("pedal : %u.%u%%\n",
                apps.pedal_per_mille / 10U,
                apps.pedal_per_mille % 10U);
      cliPrintf("command : %d / %d\n",
                apps.command_sent,
                APPS_TORQUE_MAX);
      if (apps.can_tx_muted == true)
      {
        cliPrintf("can tx : muted\n");
      }
      else
      {
        cliPrintf("can tx : %s\n", apps.can_tx_ok ? "ok" : "fail");
      }
      cliPrintf("valid : %s\n", apps.valid ? "true" : "false");
      cliPrintf("fault timer : %lums / %lums\n",
                (unsigned long)apps.fault_elapsed_ms,
                (unsigned long)APPS_FAULT_MS);
      cliPrintf("status : 0x%08lX\n", (unsigned long)status);

      if (status == APPS_STATUS_OK)
      {
        cliPrintf("  OK\n");
      }
      if ((status & APPS_STATUS_NOT_CONFIGURED) != 0U)
      {
        cliPrintf("  NOT_CONFIGURED\n");
      }
      if ((status & APPS_STATUS_ADC_READ_FAULT) != 0U)
      {
        cliPrintf("  ADC_READ_FAULT\n");
      }
      if ((status & APPS_STATUS_SIGNAL1_RANGE_FAULT) != 0U)
      {
        cliPrintf("  SIGNAL1_RANGE_FAULT\n");
      }
      if ((status & APPS_STATUS_SIGNAL2_RANGE_FAULT) != 0U)
      {
        cliPrintf("  SIGNAL2_RANGE_FAULT\n");
      }
      if ((status & APPS_STATUS_DIFF_FAULT) != 0U)
      {
        cliPrintf("  DIFF_FAULT\n");
      }
      if ((status & APPS_STATUS_FAULT_LATCHED) != 0U)
      {
        cliPrintf("  FAULT_LATCHED\n");
      }
      if ((status & APPS_STATUS_CAN_TX_FAULT) != 0U)
      {
        cliPrintf("  CAN_TX_FAULT\n");
      }

      cliPrintf("\n");

      if (show != true)
      {
        break;
      }

      delay(100);
    }

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
    cliPrintf("raw margin : -%u/+%u\n",
              APPS_RAW_LOW_TOL,
              APPS_RAW_HIGH_TOL);
    cliPrintf("can channel : %u\n", APPS_CAN_CH);
    cliPrintf("max difference : %u.%u%%\n",
              APPS_DIFF_MAX / 10U,
              APPS_DIFF_MAX % 10U);
    cliPrintf("fault confirm : %lums\n", (unsigned long)APPS_FAULT_MS);
    return;
  }

  if ((args->argc == 5) && args->isStr(0, "config"))
  {
    uint16_t raw_config[4];

    for (uint8_t i = 0; i < 4U; i++)
    {
      int32_t value = args->getData((uint8_t)(i + 1U));

      if (value < 0)
      {
        raw_config[i] = 0U;
      }
      else if (value > UINT16_MAX)
      {
        raw_config[i] = UINT16_MAX;
      }
      else
      {
        raw_config[i] = (uint16_t)value;
      }
    }

    (void)appsSetConfig(raw_config[0],
                        raw_config[1],
                        raw_config[2],
                        raw_config[3]);
    cliPrintf("signal1 raw : %u..%u\n",
              apps.raw_min[APPS_SIGNAL1],
              apps.raw_max[APPS_SIGNAL1]);
    cliPrintf("signal2 raw : %u..%u\n",
              apps.raw_min[APPS_SIGNAL2],
              apps.raw_max[APPS_SIGNAL2]);
    cliPrintf("configured : %s\n", apps.configured ? "true" : "false");
    return;
  }

  if ((args->argc == 1) && args->isStr(0, "clear"))
  {
    appsClearFault();
    cliPrintf("apps fault clear\n");
    return;
  }

  cliPrintf("apps info\n");
  cliPrintf("apps show\n");
  cliPrintf("apps config\n");
  cliPrintf("apps config signal1_min signal1_max signal2_min signal2_max\n");
  cliPrintf("apps clear\n");
}

#endif
