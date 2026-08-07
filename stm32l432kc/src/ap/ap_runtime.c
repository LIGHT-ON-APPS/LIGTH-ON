/*
 * ap_runtime.c
 *
 * ap.c에서 사용하는 소프트웨어 모듈의 초기화와 주기 실행을 통합한다.
 */

#include "ap_runtime.h"

#include "ap.h"
#include "apps_diag_can.h"
#include "can.h"
#include "display_protocol.h"
#include "sw/apps/apps.h"
#include "sw/bamocar/bamocar_can.h"
#include "sw/telemetry/telemetry.h"
#include "uart.h"


/* 표시 UART 설정과 패킷 송신 주기. */
#define DISPLAY_UART_BAUD                  115200U
#define DISPLAY_TX_PERIOD_MS               50U
#define DISPLAY_TLM_TX_PERIOD_MS           100U

/* APPS 진단 CAN 송신 설정. */
#define DIAG_CAN_TX_PERIOD_MS              50U
#define DIAG_CAN_TX_TIMEOUT_MS             1U

/* Bamocar 텔레메트리 요청 설정. */
#define BAMOCAR_REQUEST_PERIOD_MS          1000U
#define BAMOCAR_REQUEST_INTERVAL_MS        100U
#define BAMOCAR_REQUEST_TX_TIMEOUT_MS      1U

/*
 * 도로 속도 환산 계수. 팀에서 값을 확정하기 전에는 speed valid bit를
 * 설정하지 않는다.
 *
 *   바퀴 회전수 = 모터 회전수 * DRIVE_DEN / DRIVE_NUM
 */
#define VEHICLE_DRIVE_NUM                  0U
#define VEHICLE_DRIVE_DEN                  1U
#define VEHICLE_WHEEL_CIRCUMFERENCE_MM     0U

#if (VEHICLE_DRIVE_NUM > 0U) && (VEHICLE_WHEEL_CIRCUMFERENCE_MM > 0U)
#define VEHICLE_SPEED_CONFIGURED           1
#else
#define VEHICLE_SPEED_CONFIGURED           0
#endif


typedef bool (*diag_can_encoder_t)(uint8_t *p_data,
                                   size_t data_size,
                                   const apps_diag_can_sample_t *p_sample);


/* 표시 UART와 진단 CAN은 변환 없이 비교할 수 있도록 같은 flag 배치를 쓴다. */
_Static_assert(DISPLAY_PROTOCOL_FLAG_VALID == APPS_DIAG_CAN_FLAG_VALID,
               "valid flag layouts differ");
_Static_assert(DISPLAY_PROTOCOL_FLAG_CONFIGURED == APPS_DIAG_CAN_FLAG_CONFIGURED,
               "configured flag layouts differ");
_Static_assert(DISPLAY_PROTOCOL_FLAG_ADC_OK == APPS_DIAG_CAN_FLAG_ADC_OK,
               "ADC flag layouts differ");
_Static_assert(DISPLAY_PROTOCOL_FLAG_CAN_TX_OK == APPS_DIAG_CAN_FLAG_CAN_TX_OK,
               "CAN TX flag layouts differ");
_Static_assert(DISPLAY_PROTOCOL_FLAG_CAN_TX_MUTED == APPS_DIAG_CAN_FLAG_CAN_TX_MUTED,
               "CAN mute flag layouts differ");
_Static_assert(DISPLAY_PROTOCOL_FLAG_FAULT_LATCHED == APPS_DIAG_CAN_FLAG_FAULT_LATCHED,
               "fault flag layouts differ");

/* 텔레메트리 유효 비트 마스크는 표시 프로토콜로 변환 없이 전달된다. */
_Static_assert(TELEMETRY_SIGNAL_BMS_PACK_MV == DISPLAY_PROTOCOL_TLM_PACK_V,
               "pack voltage mask layouts differ");
_Static_assert(TELEMETRY_SIGNAL_BMS_PACK_MA == DISPLAY_PROTOCOL_TLM_PACK_A,
               "pack current mask layouts differ");
_Static_assert(TELEMETRY_SIGNAL_BMS_SOC_PERMILLE == DISPLAY_PROTOCOL_TLM_SOC,
               "SOC mask layouts differ");
_Static_assert(TELEMETRY_SIGNAL_BMS_MAX_TEMP_DECIC == DISPLAY_PROTOCOL_TLM_CELL_TEMP,
               "cell temperature mask layouts differ");
_Static_assert(TELEMETRY_SIGNAL_BAMOCAR_RPM == DISPLAY_PROTOCOL_TLM_RPM,
               "RPM mask layouts differ");
_Static_assert(TELEMETRY_SIGNAL_BAMOCAR_MOTOR_TEMP_DECIC == DISPLAY_PROTOCOL_TLM_MOTOR_TEMP,
               "motor temperature mask layouts differ");
_Static_assert(TELEMETRY_SIGNAL_BAMOCAR_IGBT_TEMP_DECIC == DISPLAY_PROTOCOL_TLM_IGBT_TEMP,
               "IGBT temperature mask layouts differ");


/* APPS 제어 루프 주기 상태. */
static uint32_t apps_pre_time = 0U;
static bool apps_scheduler_started = false;

/* 표시 UART 연결, 패킷 순번, 송신 주기 상태. */
static bool display_uart_open = false;
static uint16_t display_sequence = 0U;
static uint16_t display_tlm_sequence = 0U;
static uint32_t display_pre_time = 0U;
static uint32_t display_tlm_pre_time = 0U;
static bool display_scheduler_started = false;
static bool display_tlm_scheduler_started = false;

/* 진단 CAN 순번, 통계, 송신 주기 상태. */
static uint8_t diag_can_counter = 0U;
static uint32_t diag_can_tx_total = 0U;
static uint32_t diag_can_tx_fail = 0U;
static uint32_t diag_can_pre_time = 0U;
static bool diag_can_scheduler_started = false;

/* Bamocar 텔레메트리 요청 주기 상태. */
static uint32_t bamocar_request_pre_time = 0U;
static bool bamocar_scheduler_started = false;


/* --------------------------------------------------------------------------
 * APPS 페달 제어
 * -------------------------------------------------------------------------- */

/* APPS 제어의 10 ms 주기 상태만 초기화한다. 모듈 자체는 apModuleInit()이 세운다. */
static void apAppsSchedulerInit(void)
{
  apps_pre_time = 0U;
  apps_scheduler_started = false;
}

/* 최초 호출에서 기준 시각을 잡은 뒤 APPS 제어를 10 ms마다 실행한다. */
static void apAppsUpdate(void)
{
  uint32_t now_ms = millis();

  if (apps_scheduler_started != true)
  {
    apps_pre_time = now_ms;
    apps_scheduler_started = true;
    return;
  }

  if ((now_ms - apps_pre_time) >= APPS_RUN_MS)
  {
    apps_pre_time = now_ms;
    (void)appsRunPedalCommand();
  }
}

/* --------------------------------------------------------------------------
 * 공용 APPS 상태 변환
 * -------------------------------------------------------------------------- */

/* APPS 상태를 표시 UART와 진단 CAN이 공유하는 비트 플래그로 변환한다. */
static uint8_t apSnapshotMakeStateFlags(const apps_snapshot_t *p_snapshot)
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

/* --------------------------------------------------------------------------
 * 표시 장치 UART 송신
 * -------------------------------------------------------------------------- */

/* 최신 APPS 상태를 표시 프로토콜 패킷으로 만들어 UART2로 보낸다. */
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
  display_status.flags = apSnapshotMakeStateFlags(&apps_snapshot);
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

  if (displayProtocolEncodeStatus(packet, sizeof(packet), &display_status) == true)
  {
    (void)uartWrite(_DEF_UART2, packet, sizeof(packet));
  }
}

/* 설정된 감속비와 바퀴 둘레로 모터 RPM을 0.1 km/h 단위로 환산한다. */
static uint16_t apDisplayComputeRoadSpeedDkph(int32_t motor_rpm,
                                               bool *p_valid)
{
  *p_valid = false;

#if VEHICLE_SPEED_CONFIGURED
  {
    int32_t wheel_rpm;
    int32_t mm_per_min;
    int32_t dkph;

    if (motor_rpm < 0)
    {
      motor_rpm = -motor_rpm;
    }

    wheel_rpm = (motor_rpm * (int32_t)VEHICLE_DRIVE_DEN) /
                (int32_t)VEHICLE_DRIVE_NUM;
    mm_per_min = wheel_rpm * (int32_t)VEHICLE_WHEEL_CIRCUMFERENCE_MM;
    dkph = (mm_per_min * 3) / 5000;

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

/* 수집한 차량 신호를 표시용 텔레메트리 패킷으로 변환해 UART2로 보낸다. */
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

  speed_dkph = apDisplayComputeRoadSpeedDkph(signal.bamocar_rpm, &speed_valid);

  packet_data.sequence = display_tlm_sequence++;
  packet_data.source_time_ms = millis();
  packet_data.valid_mask = signal.valid_mask;
  packet_data.motor_rpm = signal.bamocar_rpm;
  packet_data.road_speed_dkph = speed_dkph;

  /* mV -> 0.1 V, mA -> 0.1 A. 팩 전류는 회생 중 음수가 된다. */
  packet_data.pack_dv = (uint16_t)((signal.bms_pack_mv < 0) ? 0 :
      ((signal.bms_pack_mv / 100) > (int32_t)UINT16_MAX ?
       (int32_t)UINT16_MAX : (signal.bms_pack_mv / 100)));
  packet_data.pack_da = (int16_t)((signal.bms_pack_ma / 100) > INT16_MAX ?
      INT16_MAX : ((signal.bms_pack_ma / 100) < INT16_MIN ?
      INT16_MIN : (signal.bms_pack_ma / 100)));
  packet_data.soc_per_mille =
      (signal.bms_soc_permille > DISPLAY_PROTOCOL_PER_MILLE_MAX) ?
      (uint16_t)DISPLAY_PROTOCOL_PER_MILLE_MAX : signal.bms_soc_permille;
  packet_data.cell_max_temp_decic = signal.bms_max_temp_decic;
  packet_data.igbt_temp_decic = signal.bamocar_igbt_temp_decic;
  packet_data.motor_temp_decic = signal.bamocar_motor_temp_decic;

  if (speed_valid == true)
  {
    packet_data.valid_mask |= DISPLAY_PROTOCOL_TLM_SPEED;
  }

  if (displayProtocolEncodeTelemetry(packet, sizeof(packet), &packet_data) == true)
  {
    (void)uartWrite(_DEF_UART2, packet, sizeof(packet));
  }
}

/* 표시 UART, 패킷 순번, 두 송신 스케줄러를 초기화한다. */
static void apDisplayInit(void)
{
  display_uart_open = uartOpen(_DEF_UART2, DISPLAY_UART_BAUD);
  display_sequence = 0U;
  display_tlm_sequence = 0U;
  display_pre_time = 0U;
  display_tlm_pre_time = 0U;
  display_scheduler_started = false;
  display_tlm_scheduler_started = false;
}

/* 최초 호출에서 기준 시각을 잡은 뒤 APPS 상태 패킷을 50 ms마다 보낸다. */
static void apDisplayStatusUpdate(void)
{
  uint32_t now_ms = millis();

  if (display_scheduler_started != true)
  {
    display_pre_time = now_ms;
    display_scheduler_started = true;
    return;
  }

  if ((now_ms - display_pre_time) >= DISPLAY_TX_PERIOD_MS)
  {
    display_pre_time = now_ms;
    apDisplaySendSnapshot();
  }
}

/* 차량 텔레메트리 패킷을 100 ms마다 보내되 다른 송신과 위상을 분리한다. */
static void apDisplayTelemetryUpdate(void)
{
  uint32_t now_ms = millis();

  if (display_tlm_scheduler_started != true)
  {
    /* APPS 상태 패킷과 같은 실행 주기에 겹치지 않도록 위상을 어긋낸다. */
    display_tlm_pre_time = now_ms - (DISPLAY_TLM_TX_PERIOD_MS / 4U);
    display_tlm_scheduler_started = true;
  }

  if ((now_ms - display_tlm_pre_time) >= DISPLAY_TLM_TX_PERIOD_MS)
  {
    display_tlm_pre_time = now_ms;
    apDisplaySendTelemetry();
  }
}

/* --------------------------------------------------------------------------
 * APPS 진단 CAN 송신
 * -------------------------------------------------------------------------- */

/* 진단 데이터 하나를 8바이트 CAN 프레임으로 인코딩해 전송한다. */
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

/* 최신 APPS 상태를 RAW, 환산값, 상태의 세 진단 프레임으로 보낸다. */
static void apDiagCanSendSnapshot(void)
{
  apps_snapshot_t apps_snapshot;
  apps_diag_can_sample_t sample;
  bool all_sent = true;

  /* 닫힌 CAN 채널을 다시 열면 APPS fault 안전 인터록이 무효화될 수 있다. */
  if (canIsOpen(APPS_CAN_CH) != true)
  {
    return;
  }

  if (appsGetSnapshot(&apps_snapshot) != true)
  {
    return;
  }

  sample.apps1_raw = apps_snapshot.raw[APPS_SIGNAL1];
  sample.apps2_raw = apps_snapshot.raw[APPS_SIGNAL2];
  sample.apps1_per_mille = apps_snapshot.percent_per_mille[APPS_SIGNAL1];
  sample.apps2_per_mille = apps_snapshot.percent_per_mille[APPS_SIGNAL2];
  sample.pedal_per_mille = apps_snapshot.pedal_per_mille;
  sample.command = apps_snapshot.command_sent;
  sample.status = apps_snapshot.status;
  sample.fault = apps_snapshot.fault_latched_status;
  sample.flags = apSnapshotMakeStateFlags(&apps_snapshot);
  sample.counter = diag_can_counter;
  sample.tx_fail_count = (diag_can_tx_fail > (uint32_t)UINT16_MAX) ?
                         (uint16_t)UINT16_MAX : (uint16_t)diag_can_tx_fail;

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

/* 진단 프레임 순번, 송신 통계, 50 ms 스케줄러를 초기화한다. */
static void apDiagCanInit(void)
{
  diag_can_counter = 0U;
  diag_can_tx_total = 0U;
  diag_can_tx_fail = 0U;
  diag_can_pre_time = 0U;
  diag_can_scheduler_started = false;
}

/* 표시 UART와 반 주기 어긋나게 진단 프레임을 50 ms마다 보낸다. */
static void apDiagCanUpdate(void)
{
  uint32_t now_ms = millis();

  if (diag_can_scheduler_started != true)
  {
    /* 표시 UART 송신과 같은 실행 주기를 피하도록 반 주기 어긋낸다. */
    diag_can_pre_time = now_ms - (DIAG_CAN_TX_PERIOD_MS / 2U);
    diag_can_scheduler_started = true;
  }

  if ((now_ms - diag_can_pre_time) >= DIAG_CAN_TX_PERIOD_MS)
  {
    diag_can_pre_time = now_ms;
    apDiagCanSendSnapshot();
  }
}

/* 누적 진단 송신 횟수와 실패 횟수를 요청한 포인터에 반환한다. */
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

/* --------------------------------------------------------------------------
 * Bamocar 텔레메트리 요청
 * -------------------------------------------------------------------------- */

/*
 * Bamocar는 요청받은 레지스터만 주기 송신한다(0x3D). 요청하는 쪽이 없으면
 * RPM도 온도도 버스에 나오지 않으므로 주기적으로 재무장한다. CAN 채널이
 * 닫혀 있으면 APPS 안전 인터록을 되돌리지 않고 이번 주기를 건너뛴다.
 */
static void apBamocarRequestTelemetry(void)
{
  static const uint8_t request_regs[] =
  {
    0x30U,   /* 실제 모터 회전수 */
    0x49U,   /* 모터 온도        */
    0x4AU,   /* IGBT 온도        */
    0xEBU,   /* DC 버스 전압     */
    0x40U,   /* 인버터 상태      */
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

    msg.id = frame.id;
    msg.data[0] = frame.data[0];
    msg.data[1] = frame.data[1];
    msg.data[2] = frame.data[2];

    /* 실패해도 재시도하지 않는다. 다음 재무장 주기에 다시 보낸다. */
    (void)canMsgWrite(APPS_CAN_CH, &msg, BAMOCAR_REQUEST_TX_TIMEOUT_MS);
  }
}

/* Bamocar 요청 스케줄러를 첫 루프에서 즉시 실행할 상태로 초기화한다. */
static void apBamocarInit(void)
{
  bamocar_request_pre_time = 0U;
  bamocar_scheduler_started = false;
}

/* 필요한 레지스터의 주기 송신을 유지하도록 1초마다 다시 요청한다. */
static void apBamocarUpdate(void)
{
  uint32_t now_ms = millis();

  if (bamocar_scheduler_started != true)
  {
    /* 첫 요청은 apMain의 첫 루프에서 즉시 전송한다. */
    bamocar_request_pre_time = now_ms - BAMOCAR_REQUEST_PERIOD_MS;
    bamocar_scheduler_started = true;
  }

  if ((now_ms - bamocar_request_pre_time) >= BAMOCAR_REQUEST_PERIOD_MS)
  {
    bamocar_request_pre_time = now_ms;
    apBamocarRequestTelemetry();
  }
}

/* --------------------------------------------------------------------------
 * 애플리케이션 런타임 진입점
 * -------------------------------------------------------------------------- */

/*
 * src/ap/sw 아래의 소프트웨어 모듈을 세운다. ADC 콜백 등록, GPIO 출력,
 * CLI 명령 등록처럼 런타임 바깥에 영향을 주는 초기화는 여기에만 둔다.
 * apInit()이 apRuntimeInit()보다 먼저 호출한다.
 */
void apModuleInit(void)
{
  appsInit();		// 페달 보정값, ADC 콜백, READY GPIO, "apps" CLI 명령.
  telemetryInit();	// Orion/Bamocar CAN 디코더 상태, "telemetry" CLI 명령.
}

/*
 * 런타임이 쓰는 주기·순번·연결 상태를 초기화한다.
 * 모듈이 이미 서 있어야 하므로 apModuleInit() 뒤에 호출해야 한다.
 */
void apRuntimeInit(void)
{
  apAppsSchedulerInit();	// APPS 제어의 10 ms 주기 상태.
  apDisplayInit();		// 계기판(F411) USART1 링크를 열고 두 패킷의 순번·송신 주기를 잡는다.
  apDiagCanInit();		// APPS 진단 CAN 프레임의 순번과 송신 통계.
  apBamocarInit();		// Bamocar 레지스터 재요청 주기. 첫 루프에서 즉시 보내도록 맞춘다.
}

/*
 * 안전 제어와 통신 처리 순서를 유지하며 각 기능을 한 번씩 실행한다.
 * apMain()의 무한 루프가 계속 호출하므로 여기서 블로킹하면 안 된다.
 * 주기 작업은 각자 millis()로 때를 확인하고 아니면 즉시 반환한다.
 */
void apRuntimeUpdate(void)
{
  /* [10 ms] 페달 ADC를 읽어 Bamocar로 토크 지령을 보낸다. 안전상 최우선. */
  apAppsUpdate();

  /* [50 ms] 같은 APPS 상태를 계기판 상태 패킷으로 만들어 USART1로 보낸다. */
  apDisplayStatusUpdate();

  /* [50 ms] 같은 APPS 상태를 진단 CAN 3프레임으로 보낸다. 위 UART와 반 주기 어긋난다. */
  apDiagCanUpdate();

  /* [1 s] Bamocar에 RPM·온도·DC버스·상태 레지스터의 주기 송신을 재요청한다. */
  apBamocarUpdate();

  /* [100 ms] 아래 telemetryUpdate()가 디코딩해 둔 차량 신호를 계기판으로 보낸다. */
  apDisplayTelemetryUpdate();

  /* 송신을 모두 마친 뒤 CAN 에러를 확인하고 bus-off에서 복구한다. */
  (void)canUpdate();

  /* CAN 수신 큐를 비우며 Orion/Bamocar 프레임을 신호 스냅샷으로 디코딩한다. */
  telemetryUpdate();
}
