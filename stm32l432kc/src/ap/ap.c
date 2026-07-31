/*
 * ap.c
 *
 *  Created on: Dec 6, 2020
 *      Author: baram
 */


#include "ap.h"
#include "apps/apps.h"
#include "apps/bamocar_can.h"
#include "signal_scope/signal_scope.h"
#include "telemetry/telemetry.h"
#include "display_protocol.h"
#include "apps_diag_can.h"


#define DISPLAY_UART_BAUD                  115200U
#define DISPLAY_TX_PERIOD_MS               50U

/* 차량 텔레메트리 표시 주기. 10 Hz. 온도·SOC는 느리게 변하고, 계기 갱신이
 * 이보다 빨라도 운전자가 읽지 못한다. 115200 baud에서 APPS 34B@20Hz +
 * 텔레메트리 36B@10Hz = 약 1.0 kB/s로 링크 용량의 10% 이하다. */
#define DISPLAY_TLM_TX_PERIOD_MS           100U

/* 진단 CAN 발행 주기. 20 Hz. */
#define DIAG_CAN_TX_PERIOD_MS              50U

/*
 * Bamocar는 요청받은 레지스터만 주기 송신한다(0x3D). 요청하는 쪽이 없으면
 * RPM도 온도도 버스에 아예 나오지 않으므로, 여기서 주기적으로 재무장한다.
 * 재무장은 같은 간격을 다시 설정하는 것뿐이라 중복 요청이 되어도 무해하고,
 * 인버터가 리셋된 뒤에도 표시가 저절로 살아난다.
 *
 * BAMOCAR_REQUEST_INTERVAL_MS의 단위는 제품 가이드에 문서화되어 있지 않다.
 * telemetry.c의 레지스터 ID들과 마찬가지로 NDrive 설정에서 확인해야 한다.
 * 값이 틀리면 해당 신호가 안 올 뿐 제어에는 영향이 없다.
 */
#define BAMOCAR_REQUEST_PERIOD_MS          1000U
#define BAMOCAR_REQUEST_INTERVAL_MS        100U

/*
 * 도로 속도 환산 계수. --- 팀에서 확정해야 하는 값 ---
 * 둘 중 하나라도 0이면 속도를 계산하지 않고 valid mask의 SPEED 비트를 세우지
 * 않는다. F411은 그 경우 숫자 대신 '--'를 띄운다. 운전자에게 근거 없는 속도를
 * 보여주는 것보다 아무것도 안 보여주는 편이 낫다.
 *
 *   바퀴 회전수 = 모터 회전수 * DRIVE_DEN / DRIVE_NUM
 */
#define VEHICLE_DRIVE_NUM                  0U    /* 감속비 분자 (모터측) */
#define VEHICLE_DRIVE_DEN                  1U    /* 감속비 분모 (바퀴측) */
#define VEHICLE_WHEEL_CIRCUMFERENCE_MM     0U    /* 타이어 둘레 [mm]     */

/* Bamocar 명령 경로와 같은 송신 상한. mailbox에 여유가 있으면 드라이버가
 * 대기 없이 반환하고, 세 개가 모두 차 있으면 즉시 실패한다. 진단 프레임은
 * 재시도하지 않으므로 이 값이 제어 루프가 감수하는 최대 지연이다. */
#define DIAG_CAN_TX_TIMEOUT_MS             1U


static bool display_uart_open 		= false;
static uint16_t display_sequence 	= 0U;
static uint16_t display_tlm_sequence 	= 0U;
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

/*
 * 모터 회전수를 0.1 km/h 단위 도로 속도로 바꾼다. 환산 계수가 설정되지 않았으면
 * 0을 돌려주고, 호출측이 valid 비트를 세우지 않는다.
 *
 * 중간값은 int32에 들어간다: 6500 rpm / 감속비 4 * 둘레 1900 mm = 약 3.1e6,
 * 여기에 3을 곱해도 9.3e6으로 여유가 있다.
 */
/* 계수가 설정되기 전에는 계산 코드 자체를 뺀다. 런타임 if로만 막으면 컴파일러가
 * 상수 0 나눗셈을 보고 경고한다(-Wdiv-by-zero). */
#if (VEHICLE_DRIVE_NUM > 0U) && (VEHICLE_WHEEL_CIRCUMFERENCE_MM > 0U)
#define VEHICLE_SPEED_CONFIGURED           1
#else
#define VEHICLE_SPEED_CONFIGURED           0
#endif

static uint16_t apComputeRoadSpeedDkph(int32_t motor_rpm, bool *p_valid)
{
  *p_valid = false;

#if VEHICLE_SPEED_CONFIGURED
  {
    int32_t wheel_rpm;
    int32_t mm_per_min;
    int32_t dkph;

    /* 후진/회생 중 음수 rpm은 속도계에서 크기만 본다. */
    if (motor_rpm < 0)
    {
      motor_rpm = -motor_rpm;
    }

    wheel_rpm  = (motor_rpm * (int32_t)VEHICLE_DRIVE_DEN) /
                 (int32_t)VEHICLE_DRIVE_NUM;
    mm_per_min = wheel_rpm * (int32_t)VEHICLE_WHEEL_CIRCUMFERENCE_MM;
    /* mm/min -> 0.1 km/h : *60 /1e6 *10  ==  *3 /5000 */
    dkph       = (mm_per_min * 3) / 5000;

    if (dkph > (int32_t)UINT16_MAX)
    {
      dkph = (int32_t)UINT16_MAX;
    }

    *p_valid = true;
    return (uint16_t)dkph;
  }
#else
  (void)motor_rpm;
  return 0U;
#endif
}

static void apDisplaySendTelemetry(void)
{
  telemetry_signal_snapshot_t signal;
  display_protocol_telemetry_t packet_data;
  uint8_t packet[DISPLAY_PROTOCOL_TELEMETRY_SIZE];
  bool speed_valid = false;
  uint16_t speed_dkph;

  if ((display_uart_open != true) ||
      (telemetryGetSignalSnapshot(&signal) != true))
  {
    return;
  }

  speed_dkph = apComputeRoadSpeedDkph(signal.bamocar_rpm, &speed_valid);

  packet_data.sequence 		= display_tlm_sequence++;
  packet_data.source_time_ms 	= millis();
  /* 텔레메트리 비트 배치를 표시 프로토콜과 동일하게 유지하므로 그대로 넘긴다.
   * 어느 한쪽 비트를 바꾸면 표시가 조용히 엉뚱한 필드를 무효로 처리한다. */
  packet_data.valid_mask 	= signal.valid_mask;
  packet_data.motor_rpm 	= signal.bamocar_rpm;
  packet_data.road_speed_dkph 	= speed_dkph;

  /* mV -> 0.1 V, mA -> 0.1 A. 팩 전압은 음수가 될 수 없고, 팩 전류는 회생 중
   * 음수가 된다(+ = 방전). */
  packet_data.pack_dv 		= (uint16_t)((signal.bms_pack_mv < 0) ? 0 :
                                  ((signal.bms_pack_mv / 100) > (int32_t)UINT16_MAX ?
                                   (int32_t)UINT16_MAX : (signal.bms_pack_mv / 100)));
  packet_data.pack_da 		= (int16_t)((signal.bms_pack_ma / 100) > INT16_MAX ? INT16_MAX :
                                 ((signal.bms_pack_ma / 100) < INT16_MIN ? INT16_MIN :
                                  (signal.bms_pack_ma / 100)));

  packet_data.soc_per_mille 	= (signal.bms_soc_permille >
                                   DISPLAY_PROTOCOL_PER_MILLE_MAX) ?
                                  (uint16_t)DISPLAY_PROTOCOL_PER_MILLE_MAX :
                                  signal.bms_soc_permille;
  packet_data.cell_max_temp_decic = signal.bms_max_temp_decic;
  packet_data.igbt_temp_decic 	= signal.bamocar_igbt_temp_decic;
  packet_data.motor_temp_decic 	= signal.bamocar_motor_temp_decic;

  if (speed_valid == true)
  {
    packet_data.valid_mask |= DISPLAY_PROTOCOL_TLM_SPEED;
  }

  if (displayProtocolEncodeTelemetry(packet, sizeof(packet), &packet_data) ==
      true)
  {
    (void)uartWrite(_DEF_UART2, packet, sizeof(packet));
  }
}

/*
 * Bamocar에 주기 송신을 요청한다. 요청하지 않으면 인버터는 아무것도 보내지
 * 않는다. 토크 명령과 같은 0x201로 나가지만 레지스터가 0x3D(read request)라
 * 명령 경로와 섞이지 않는다.
 *
 * 진단 CAN과 같은 이유로, 채널이 닫혀 있으면 열지 않고 건너뛴다. fault 확정 후
 * appsSendCommand()가 canClose()로 만든 물리적 차단을 표시 기능이 되돌리면
 * 안 된다.
 */
static void apBamocarRequestTelemetry(void)
{
  static const uint8_t request_regs[] =
  {
    0x30U,   /* N_actual   : 모터 회전수 */
    0x49U,   /* motor temp                */
    0x4AU,   /* IGBT temp                 */
    0xEBU,   /* DC bus voltage            */
    0x40U,   /* status                    */
  };
  bamocar_can_frame_t frame;
  can_msg_t msg;

  if (canIsOpen(APPS_CAN_CH) != true)
  {
    return;
  }

  for (size_t i = 0U; i < (sizeof(request_regs) / sizeof(request_regs[0])); i++)
  {
    bamocarCanMakeReadRequest(BAMOCAR_CAN_DEFAULT_RX_ID,
                              request_regs[i],
                              BAMOCAR_REQUEST_INTERVAL_MS,
                              &frame);

    if (canMsgInit(&msg, CAN_CLASSIC, CAN_STD, CAN_DLC_3) != true)
    {
      return;
    }

    msg.id      = frame.id;
    msg.data[0] = frame.data[0];
    msg.data[1] = frame.data[1];
    msg.data[2] = frame.data[2];

    /* 실패해도 재시도하지 않는다. 다음 재무장 주기에 다시 보낸다. */
    (void)canMsgWrite(APPS_CAN_CH, &msg, DIAG_CAN_TX_TIMEOUT_MS);
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

  /* 표시용 차량 신호(RPM, 셀/IGBT 온도, 팩 전압·전류·SOC)는 이 모듈이 CAN에서
   * 디코딩한다. FatFS 로깅과는 독립이며, 로깅을 켜지 않으면 수신·해석만 한다. */
  (void)telemetryInit();

//  guiInit();
}

void apMain(void)
{
  uint32_t apps_pre_time;
  uint32_t display_pre_time;
  uint32_t diag_can_pre_time;
  uint32_t display_tlm_pre_time;
  uint32_t bamocar_request_pre_time;

  apps_pre_time 	= millis();
  display_pre_time 	= apps_pre_time;

  /* 표시 UART 송신과 진단 CAN 송신이 같은 사이클에 겹치지 않도록 위상을 반 주기
   * 어긋낸다. 과거 시각으로 두어야 한다. 미래로 두면 부호 없는 뺄셈이 wrap되어
   * 첫 판정에서 곧바로 실행된다. */
  diag_can_pre_time = apps_pre_time - (DIAG_CAN_TX_PERIOD_MS / 2U);

  /* 텔레메트리 UART도 APPS 패킷과 같은 사이클을 피한다. */
  display_tlm_pre_time = apps_pre_time - (DISPLAY_TLM_TX_PERIOD_MS / 4U);

  /* 첫 요청은 기다리지 않고 곧바로 나가게 한다. 그래야 부팅 직후부터 계기에
   * 값이 뜬다. */
  bamocar_request_pre_time = apps_pre_time - BAMOCAR_REQUEST_PERIOD_MS;

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

    if (now_ms-bamocar_request_pre_time >= BAMOCAR_REQUEST_PERIOD_MS)
    {
      bamocar_request_pre_time = now_ms;
      apBamocarRequestTelemetry();
    }

    if (now_ms-display_tlm_pre_time >= DISPLAY_TLM_TX_PERIOD_MS)
    {
      display_tlm_pre_time = now_ms;
      apDisplaySendTelemetry();
    }

    (void)canUpdate();

    /* CAN 수신 큐를 비우고 Bamocar/Orion 프레임을 해석한다. 호출 시점에 쌓여
     * 있던 개수만큼만 처리하므로 이 호출이 무한정 길어지지는 않는다. 비우지
     * 않으면 큐가 가득 차서 이후 수신이 버려진다. */
    telemetryUpdate();

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
