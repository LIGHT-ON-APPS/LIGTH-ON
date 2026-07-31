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
#include "apps_diag_can.h"


#define DISPLAY_UART_BAUD                  115200U
#define DISPLAY_TX_PERIOD_MS               50U

/* 진단 CAN 발행 주기. 20 Hz. */
#define DIAG_CAN_TX_PERIOD_MS              50U

/* Bamocar 명령 경로와 같은 송신 상한. mailbox에 여유가 있으면 드라이버가
 * 대기 없이 반환하고, 세 개가 모두 차 있으면 즉시 실패한다. 진단 프레임은
 * 재시도하지 않으므로 이 값이 제어 루프가 감수하는 최대 지연이다. */
#define DIAG_CAN_TX_TIMEOUT_MS             1U


static bool display_uart_open 		= false;
static uint16_t display_sequence 	= 0U;
static volatile bool service_mode 	= false;
static bool service_mode_active 	= false;
static uint8_t diag_can_counter 	= 0U;
static uint32_t diag_can_tx_total 	= 0U;
static uint32_t diag_can_tx_fail 	= 0U;


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


/* 표시 UART와 진단 CAN이 같은 flags 바이트를 공유한다. 두 경로를 대조할 때
 * 변환 없이 비교할 수 있도록 shared/display_protocol.h의 DISPLAY_PROTOCOL_FLAG_*
 * 와 shared/apps_diag_can.h의 APPS_DIAG_CAN_FLAG_* 값을 동일하게 유지한다.
 * 한쪽만 바꾸면 대조가 조용히 깨진다. */
static uint8_t apMakeStateFlags(const apps_snapshot_t *p_snapshot)
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

  if ((display_uart_open != true) || (appsGetSnapshot(&apps_snapshot) != true))
  {
    return;
  }

  display_status.sequence 			= display_sequence++;
  display_status.flags 				= apMakeStateFlags(&apps_snapshot);
  display_status.source_time_ms 	= millis();

  display_status.apps1_raw 			= apps_snapshot.raw[APPS_SIGNAL1];
  display_status.apps2_raw 			= apps_snapshot.raw[APPS_SIGNAL2];
  display_status.apps1_per_mille 	= apps_snapshot.percent_per_mille[APPS_SIGNAL1];
  display_status.apps2_per_mille 	= apps_snapshot.percent_per_mille[APPS_SIGNAL2];
  display_status.pedal_per_mille 	= apps_snapshot.pedal_per_mille;
  display_status.command 			= apps_snapshot.command_sent;
  display_status.status 			= apps_snapshot.status;
  display_status.fault 				= apps_snapshot.fault_latched_status;

  if (displayProtocolEncodeStatus(packet, sizeof(packet), &display_status) == true)
  {
    (void)uartWrite(_DEF_UART2, packet, sizeof(packet));
  }
}

typedef bool (*diag_can_encoder_t)(uint8_t *p_data,
                                   size_t data_size,
                                   const apps_diag_can_sample_t *p_sample);

static bool apDiagCanSendFrame(uint32_t id,
                               diag_can_encoder_t encoder,
                               const apps_diag_can_sample_t *p_sample)
{
  can_msg_t msg;

  if (canMsgInit(&msg, CAN_CLASSIC, CAN_STD, CAN_DLC_8) != true)
  {
    return false;
  }

  msg.id = id;

  if (encoder(msg.data, sizeof(msg.data), p_sample) != true)
  {
    return false;
  }

  return canMsgWrite(APPS_CAN_CH, &msg, DIAG_CAN_TX_TIMEOUT_MS);
}

static void apDiagCanSendSnapshot(void)
{
  apps_snapshot_t apps_snapshot;
  apps_diag_can_sample_t sample;
  bool all_sent = true;

  /* 진단은 안전 경로에 개입하지 않는다. _DEF_VER1에서 타당성 fault가 확정되면
   * appsSendCommand()가 canClose()로 토크 명령 경로를 물리적으로 끊는데, 진단이
   * 채널을 다시 열면 그 보장이 사라진다. 닫혀 있으면 이 주기를 건너뛴다.
   * 결과적으로 fault 확정 이후에는 진단 프레임도 멈추므로, 서버에서는 프레임이
   * 끊긴 것 자체를 fault 신호로 함께 읽어야 한다. 확정 직전 100 ms(APPS_FAULT_MS)
   * 동안의 진행 상황은 정상적으로 발행된다. 표시 UART는 CAN과 무관하게 계속
   * 동작하므로 fault 이후 상태는 F411에서 확인할 수 있다. */
  if (canIsOpen(APPS_CAN_CH) != true)
  {
    return;
  }

  if (appsGetSnapshot(&apps_snapshot) != true)
  {
    return;
  }

  sample.apps1_raw 		= apps_snapshot.raw[APPS_SIGNAL1];
  sample.apps2_raw 		= apps_snapshot.raw[APPS_SIGNAL2];
  sample.apps1_per_mille 	= apps_snapshot.percent_per_mille[APPS_SIGNAL1];
  sample.apps2_per_mille 	= apps_snapshot.percent_per_mille[APPS_SIGNAL2];
  sample.pedal_per_mille 	= apps_snapshot.pedal_per_mille;
  sample.command 			= apps_snapshot.command_sent;
  sample.status 			= apps_snapshot.status;
  sample.fault 				= apps_snapshot.fault_latched_status;
  sample.flags 				= apMakeStateFlags(&apps_snapshot);
  sample.counter 			= diag_can_counter;
  sample.tx_fail_count 		= (diag_can_tx_fail > (uint32_t)UINT16_MAX) ?
                     		  (uint16_t)UINT16_MAX : (uint16_t)diag_can_tx_fail;

  /* 세 프레임은 한 세트로 같은 counter를 싣는다. 앞 프레임이 실패해도 뒤를
   * 계속 보내서, 서버가 어떤 프레임이 빠졌는지 구분할 수 있게 한다. */
  if (apDiagCanSendFrame(APPS_DIAG_CAN_ID_RAW,
                         appsDiagCanEncodeRaw, &sample) != true)
  {
    all_sent = false;
  }
  if (apDiagCanSendFrame(APPS_DIAG_CAN_ID_SCALED,
                         appsDiagCanEncodeScaled, &sample) != true)
  {
    all_sent = false;
  }
  if (apDiagCanSendFrame(APPS_DIAG_CAN_ID_STATUS,
                         appsDiagCanEncodeStatus, &sample) != true)
  {
    all_sent = false;
  }

  diag_can_counter++;
  diag_can_tx_total++;

  if ((all_sent != true) && (diag_can_tx_fail < UINT32_MAX))
  {
    diag_can_tx_fail++;
  }
}

void apDiagCanGetStats(uint32_t *p_tx_total, uint32_t *p_tx_fail)
{
  if (p_tx_total != NULL)
  {
    *p_tx_total = diag_can_tx_total;
  }
  if (p_tx_fail != NULL)
  {
    *p_tx_fail = diag_can_tx_fail;
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
  uint32_t diag_can_pre_time;

  apps_pre_time 	= millis();
  display_pre_time 	= apps_pre_time;

  /* 표시 UART 송신과 진단 CAN 송신이 같은 사이클에 겹치지 않도록 위상을 반 주기
   * 어긋낸다. 과거 시각으로 두어야 한다. 미래로 두면 부호 없는 뺄셈이 wrap되어
   * 첫 판정에서 곧바로 실행된다. */
  diag_can_pre_time = apps_pre_time - (DIAG_CAN_TX_PERIOD_MS / 2U);

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

    if (now_ms-diag_can_pre_time >= DIAG_CAN_TX_PERIOD_MS)
    {
      diag_can_pre_time = now_ms;
      apDiagCanSendSnapshot();
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
