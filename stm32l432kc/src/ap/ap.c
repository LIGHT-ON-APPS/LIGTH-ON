/*
 * ap.c
 *
 *  Created on: Dec 6, 2020
 *      Author: baram
 */


#include "ap.h"
#include "apps/apps.h"
#include "signal_scope/signal_scope.h"
#include "telemetry/telemetry.h"
#include "display_protocol.h"


#define DISPLAY_UART_BAUD                  115200U
#define DISPLAY_TX_PERIOD_MS               50U


static bool display_uart_open = false;
static uint16_t display_sequence = 0U;
static volatile bool service_mode = false;
static bool service_mode_active = false;


static void apUpdateServiceMode(void)
{
#ifdef _USE_HW_CLI
  bool requested = service_mode;

  if ((requested == true) && (service_mode_active != true))
  {
    service_mode_active = cliOpen(_DEF_UART1, 57600);
    if (service_mode_active != true)
    {
      service_mode = false;
    }
  }
  else if ((requested != true) && (service_mode_active == true))
  {
    (void)cliClose();
    service_mode_active = false;
  }
#endif
}


static uint8_t apDisplayMakeFlags(const apps_snapshot_t *p_snapshot)
{
  uint8_t flags = 0U;

  if (p_snapshot->valid == true)
  {
    flags |= DISPLAY_PROTOCOL_FLAG_VALID;
  }
  if (p_snapshot->configured == true)
  {
    flags |= DISPLAY_PROTOCOL_FLAG_CONFIGURED;
  }
  if (p_snapshot->adc_ok == true)
  {
    flags |= DISPLAY_PROTOCOL_FLAG_ADC_OK;
  }
  if (p_snapshot->can_tx_ok == true)
  {
    flags |= DISPLAY_PROTOCOL_FLAG_CAN_TX_OK;
  }
  if (p_snapshot->can_tx_muted == true)
  {
    flags |= DISPLAY_PROTOCOL_FLAG_CAN_TX_MUTED;
  }
  if (p_snapshot->fault_latched == true)
  {
    flags |= DISPLAY_PROTOCOL_FLAG_FAULT_LATCHED;
  }

  return flags;
}

static void apDisplaySendSnapshot(void)
{
  apps_snapshot_t apps_snapshot;
  display_protocol_status_t display_status;
  uint8_t packet[DISPLAY_PROTOCOL_PACKET_SIZE];

  if ((display_uart_open != true) ||
      (appsGetSnapshot(&apps_snapshot) != true))
  {
    return;
  }

  display_status.sequence = display_sequence++;
  display_status.flags = apDisplayMakeFlags(&apps_snapshot);
  display_status.source_time_ms = millis();
  display_status.apps1_raw = apps_snapshot.raw[APPS_SIGNAL1];
  display_status.apps2_raw = apps_snapshot.raw[APPS_SIGNAL2];
  display_status.apps1_per_mille =
      apps_snapshot.percent_per_mille[APPS_SIGNAL1];
  display_status.apps2_per_mille =
      apps_snapshot.percent_per_mille[APPS_SIGNAL2];
  display_status.pedal_per_mille = apps_snapshot.pedal_per_mille;
  display_status.command = apps_snapshot.command_sent;
  display_status.status = apps_snapshot.status;
  display_status.fault = apps_snapshot.fault_latched_status;

  if (displayProtocolEncodeStatus(packet,
                                  sizeof(packet),
                                  &display_status) == true)
  {
    (void)uartWrite(_DEF_UART2, packet, sizeof(packet));
  }
}

void apSetServiceMode(bool enabled)
{
  service_mode = enabled;
}

bool apIsServiceMode(void)
{
  return service_mode;
}


void apInit(void)
{
#ifdef _USE_HW_CLI
  cliSetRunCheck(apIsServiceMode);
#endif

  appsInit();
  scopeInit();
  display_uart_open = uartOpen(_DEF_UART2, DISPLAY_UART_BAUD);

//  telemetryInit();
//  guiInit();
}

void apMain(void)
{
  uint32_t apps_pre_time;
  uint32_t display_pre_time;

  apps_pre_time = millis();
  display_pre_time = apps_pre_time;

  while(1)
  {
    uint32_t now_ms = millis();

    apUpdateServiceMode();

    if (now_ms-apps_pre_time >= APPS_RUN_MS)
    {
      apps_pre_time = now_ms;
      (void)appsRunPedalCommand();
    }

    if (now_ms-display_pre_time >= DISPLAY_TX_PERIOD_MS)
    {
      display_pre_time = now_ms;
      apDisplaySendSnapshot();
    }

    (void)canUpdate();

#ifdef _USE_HW_CLI
    if ((service_mode == true) && (service_mode_active == true))
    {
      cliMain();
      scopeUpdate();
    }
#endif
//    telemetryUpdate();
  }
}
