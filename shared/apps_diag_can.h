#ifndef SHARED_APPS_DIAG_CAN_H_
#define SHARED_APPS_DIAG_CAN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


/*
 * L432 -> vehicle CAN bus, APPS diagnostic frames, layout version 1.
 *
 * TMA-1은 L432와 F411 사이의 표시 UART를 볼 수 없다. 따라서 APPS 판단 결과를
 * 서버에 남기려면 L432가 CAN으로 따로 발행해야 한다. 이 헤더는 그 프레임
 * 레이아웃의 유일한 기준이며 서버 파서도 이 파일을 참조한다.
 *
 * 규칙
 *  - 모든 다중 바이트 필드는 little-endian이다. C 구조체를 그대로 전송하지 않는다.
 *  - 프레임 자체 CRC는 두지 않는다. 8바이트를 모두 쓰며, 전송 오류는 CAN
 *    컨트롤러의 15비트 CRC와 ACK/재전송이 이미 걸러낸다. 대신 세 프레임 모두
 *    마지막 바이트에 같은 rolling counter를 실어 주기 누락을 감지한다.
 *  - 진단 ID는 제어 프레임 ID를 재사용하지 않는다. Bamocar는 0x201(명령)과
 *    0x181(응답)을 쓴다. 표준 11비트에서 ID가 클수록 우선순위가 낮으므로
 *    0x7A0대를 써서 제어 트래픽을 밀어내지 않는다.
 *
 * 프레임 A  ID 0x7A0  DLC 8  원시값과 최종 페달
 *   0  APPS1 raw            uint16  ADC count
 *   2  APPS2 raw            uint16  ADC count
 *   4  final pedal          uint16  per-mille (0..1000)
 *   6  state flags          uint8   APPS_DIAG_CAN_FLAG_*
 *   7  rolling counter      uint8
 *
 * 프레임 B  ID 0x7A1  DLC 8  보정값과 송신한 명령
 *   0  APPS1 position       uint16  per-mille (0..1000)
 *   2  APPS2 position       uint16  per-mille (0..1000)
 *   4  sent command         int16   Bamocar raw torque command
 *   6  reserved             uint8   0
 *   7  rolling counter      uint8
 *
 * 프레임 C  ID 0x7A2  DLC 8  상태와 fault
 *   0  current status       uint16  apps_status_t 하위 16비트
 *   2  latched fault        uint16  apps_status_t 하위 16비트
 *   4  diag tx fail count   uint16  포화 카운터, 이 진단 경로 자체의 송신 실패
 *   6  reserved             uint8   0
 *   7  rolling counter      uint8
 *
 * status/fault를 16비트로 자르는 것은 현재 apps_status_t의 최상위 비트가
 * 0x40(APPS_STATUS_CAN_TX_FAULT)이기 때문이다. 비트를 0x8000 이상으로 확장하면
 * 이 프레임 레이아웃과 서버 파서를 함께 개정해야 한다.
 */

#define APPS_DIAG_CAN_LAYOUT_VERSION       1U
#define APPS_DIAG_CAN_FRAME_SIZE           8U
#define APPS_DIAG_CAN_PER_MILLE_MAX        1000U

#define APPS_DIAG_CAN_BASE_ID              0x7A0U
#define APPS_DIAG_CAN_ID_RAW               (APPS_DIAG_CAN_BASE_ID + 0U)
#define APPS_DIAG_CAN_ID_SCALED            (APPS_DIAG_CAN_BASE_ID + 1U)
#define APPS_DIAG_CAN_ID_STATUS            (APPS_DIAG_CAN_BASE_ID + 2U)

/* 표시 프로토콜과 같은 비트 배치를 쓴다. 두 경로를 대조할 때 flags를 변환
 * 없이 비교할 수 있어야 한다. shared/display_protocol.h와 함께 개정한다. */
#define APPS_DIAG_CAN_FLAG_VALID           (1U << 0)
#define APPS_DIAG_CAN_FLAG_CONFIGURED      (1U << 1)
#define APPS_DIAG_CAN_FLAG_ADC_OK          (1U << 2)
#define APPS_DIAG_CAN_FLAG_CAN_TX_OK       (1U << 3)
#define APPS_DIAG_CAN_FLAG_CAN_TX_MUTED    (1U << 4)
#define APPS_DIAG_CAN_FLAG_FAULT_LATCHED   (1U << 5)


typedef struct
{
  uint16_t apps1_raw;
  uint16_t apps2_raw;
  uint16_t apps1_per_mille;
  uint16_t apps2_per_mille;
  uint16_t pedal_per_mille;
  int16_t  command;
  uint32_t status;
  uint32_t fault;
  uint16_t tx_fail_count;
  uint8_t  flags;
  uint8_t  counter;
} apps_diag_can_sample_t;


static inline void appsDiagCanPutU16Le(uint8_t *p_data, uint16_t value)
{
  p_data[0] = (uint8_t)(value & 0xFFU);
  p_data[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static inline uint16_t appsDiagCanGetU16Le(const uint8_t *p_data)
{
  return (uint16_t)((uint16_t)p_data[0] |
                    ((uint16_t)p_data[1] << 8));
}

static inline bool appsDiagCanSampleIsValid(
    const apps_diag_can_sample_t *p_sample)
{
  if (p_sample == NULL)
  {
    return false;
  }
  if ((p_sample->apps1_per_mille > APPS_DIAG_CAN_PER_MILLE_MAX) ||
      (p_sample->apps2_per_mille > APPS_DIAG_CAN_PER_MILLE_MAX) ||
      (p_sample->pedal_per_mille > APPS_DIAG_CAN_PER_MILLE_MAX))
  {
    return false;
  }

  return true;
}

static inline bool appsDiagCanEncodeRaw(
    uint8_t *p_data,
    size_t data_size,
    const apps_diag_can_sample_t *p_sample)
{
  if ((p_data == NULL) ||
      (data_size < APPS_DIAG_CAN_FRAME_SIZE) ||
      (appsDiagCanSampleIsValid(p_sample) != true))
  {
    return false;
  }

  appsDiagCanPutU16Le(&p_data[0], p_sample->apps1_raw);
  appsDiagCanPutU16Le(&p_data[2], p_sample->apps2_raw);
  appsDiagCanPutU16Le(&p_data[4], p_sample->pedal_per_mille);
  p_data[6] = p_sample->flags;
  p_data[7] = p_sample->counter;

  return true;
}

static inline bool appsDiagCanEncodeScaled(
    uint8_t *p_data,
    size_t data_size,
    const apps_diag_can_sample_t *p_sample)
{
  if ((p_data == NULL) ||
      (data_size < APPS_DIAG_CAN_FRAME_SIZE) ||
      (appsDiagCanSampleIsValid(p_sample) != true))
  {
    return false;
  }

  appsDiagCanPutU16Le(&p_data[0], p_sample->apps1_per_mille);
  appsDiagCanPutU16Le(&p_data[2], p_sample->apps2_per_mille);
  appsDiagCanPutU16Le(&p_data[4], (uint16_t)p_sample->command);
  p_data[6] = 0U;
  p_data[7] = p_sample->counter;

  return true;
}

static inline bool appsDiagCanEncodeStatus(
    uint8_t *p_data,
    size_t data_size,
    const apps_diag_can_sample_t *p_sample)
{
  if ((p_data == NULL) ||
      (data_size < APPS_DIAG_CAN_FRAME_SIZE) ||
      (appsDiagCanSampleIsValid(p_sample) != true))
  {
    return false;
  }

  appsDiagCanPutU16Le(&p_data[0], (uint16_t)(p_sample->status & 0xFFFFU));
  appsDiagCanPutU16Le(&p_data[2], (uint16_t)(p_sample->fault & 0xFFFFU));
  appsDiagCanPutU16Le(&p_data[4], p_sample->tx_fail_count);
  p_data[6] = 0U;
  p_data[7] = p_sample->counter;

  return true;
}


#ifdef __cplusplus
}
#endif

#endif /* SHARED_APPS_DIAG_CAN_H_ */
