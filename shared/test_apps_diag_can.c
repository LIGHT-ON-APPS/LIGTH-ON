#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "apps_diag_can.h"


/* 서버 파서가 이 바이트 배치에 의존한다. 레이아웃을 바꾸면 이 벡터가 먼저
 * 깨지도록 세 프레임을 고정해 둔다. */
static const apps_diag_can_sample_t sample_golden =
{
  .apps1_raw       = 0x0123U,
  .apps2_raw       = 0x0456U,
  .apps1_per_mille = 498U,
  .apps2_per_mille = 502U,
  .pedal_per_mille = 500U,
  .command         = -1234,
  .status          = 0x00000010U,  /* DIFF_FAULT */
  .fault           = 0x00000030U,  /* DIFF_FAULT | FAULT_LATCHED */
  .tx_fail_count   = 7U,
  .flags           = APPS_DIAG_CAN_FLAG_VALID |
                     APPS_DIAG_CAN_FLAG_CONFIGURED |
                     APPS_DIAG_CAN_FLAG_ADC_OK,
  .counter         = 0x2AU,
};

static void testGoldenRawFrame(void)
{
  static const uint8_t expected[APPS_DIAG_CAN_FRAME_SIZE] =
  {
    0x23U, 0x01U,   /* APPS1 raw 0x0123 little-endian */
    0x56U, 0x04U,   /* APPS2 raw 0x0456 */
    0xF4U, 0x01U,   /* final pedal 500 */
    0x07U,          /* flags VALID|CONFIGURED|ADC_OK */
    0x2AU,          /* rolling counter */
  };
  uint8_t data[APPS_DIAG_CAN_FRAME_SIZE];

  memset(data, 0xA5U, sizeof(data));
  assert(appsDiagCanEncodeRaw(data, sizeof(data), &sample_golden) == true);
  assert(memcmp(data, expected, sizeof(expected)) == 0);
}

static void testGoldenScaledFrame(void)
{
  static const uint8_t expected[APPS_DIAG_CAN_FRAME_SIZE] =
  {
    0xF2U, 0x01U,   /* APPS1 position 498 */
    0xF6U, 0x01U,   /* APPS2 position 502 */
    0x2EU, 0xFBU,   /* sent command -1234 as two's complement */
    0x00U,          /* reserved */
    0x2AU,
  };
  uint8_t data[APPS_DIAG_CAN_FRAME_SIZE];

  memset(data, 0xA5U, sizeof(data));
  assert(appsDiagCanEncodeScaled(data, sizeof(data), &sample_golden) == true);
  assert(memcmp(data, expected, sizeof(expected)) == 0);
}

static void testGoldenStatusFrame(void)
{
  static const uint8_t expected[APPS_DIAG_CAN_FRAME_SIZE] =
  {
    0x10U, 0x00U,   /* current status 0x0010 */
    0x30U, 0x00U,   /* latched fault 0x0030 */
    0x07U, 0x00U,   /* diag tx fail count 7 */
    0x00U,          /* reserved */
    0x2AU,
  };
  uint8_t data[APPS_DIAG_CAN_FRAME_SIZE];

  memset(data, 0xA5U, sizeof(data));
  assert(appsDiagCanEncodeStatus(data, sizeof(data), &sample_golden) == true);
  assert(memcmp(data, expected, sizeof(expected)) == 0);
}

/* 세 프레임은 한 세트이므로 counter가 모두 같아야 한다. 서버는 이 값으로
 * 세트를 묶고 주기 누락을 센다. */
static void testCounterIsSharedAcrossFrames(void)
{
  uint8_t raw[APPS_DIAG_CAN_FRAME_SIZE];
  uint8_t scaled[APPS_DIAG_CAN_FRAME_SIZE];
  uint8_t status[APPS_DIAG_CAN_FRAME_SIZE];

  assert(appsDiagCanEncodeRaw(raw, sizeof(raw), &sample_golden) == true);
  assert(appsDiagCanEncodeScaled(scaled, sizeof(scaled), &sample_golden) == true);
  assert(appsDiagCanEncodeStatus(status, sizeof(status), &sample_golden) == true);

  assert(raw[7] == sample_golden.counter);
  assert(scaled[7] == sample_golden.counter);
  assert(status[7] == sample_golden.counter);
}

/* status/fault는 16비트로 잘린다. 상위 비트를 쓰기 시작하면 레이아웃과 서버
 * 파서를 함께 개정해야 하므로, 절단이 실제로 일어남을 명시적으로 고정한다. */
static void testStatusTruncatesToLow16Bits(void)
{
  apps_diag_can_sample_t sample = sample_golden;
  uint8_t data[APPS_DIAG_CAN_FRAME_SIZE];

  sample.status = 0xDEAD0011U;
  sample.fault  = 0xBEEF0022U;

  assert(appsDiagCanEncodeStatus(data, sizeof(data), &sample) == true);
  assert(appsDiagCanGetU16Le(&data[0]) == 0x0011U);
  assert(appsDiagCanGetU16Le(&data[2]) == 0x0022U);
}

static void testEndianRoundTrip(void)
{
  uint8_t data[2];

  appsDiagCanPutU16Le(data, 0x1234U);
  assert(data[0] == 0x34U);
  assert(data[1] == 0x12U);
  assert(appsDiagCanGetU16Le(data) == 0x1234U);

  appsDiagCanPutU16Le(data, 0U);
  assert(appsDiagCanGetU16Le(data) == 0U);
  appsDiagCanPutU16Le(data, 0xFFFFU);
  assert(appsDiagCanGetU16Le(data) == 0xFFFFU);
}

static void testArgumentAndRangeValidation(void)
{
  uint8_t data[APPS_DIAG_CAN_FRAME_SIZE];
  apps_diag_can_sample_t sample = sample_golden;

  /* NULL 인자 */
  assert(appsDiagCanEncodeRaw(NULL, sizeof(data), &sample_golden) == false);
  assert(appsDiagCanEncodeRaw(data, sizeof(data), NULL) == false);
  assert(appsDiagCanEncodeScaled(data, sizeof(data), NULL) == false);
  assert(appsDiagCanEncodeStatus(data, sizeof(data), NULL) == false);

  /* 버퍼가 프레임보다 작으면 거부 */
  assert(appsDiagCanEncodeRaw(data, APPS_DIAG_CAN_FRAME_SIZE - 1U,
                              &sample_golden) == false);
  assert(appsDiagCanEncodeScaled(data, APPS_DIAG_CAN_FRAME_SIZE - 1U,
                                 &sample_golden) == false);
  assert(appsDiagCanEncodeStatus(data, APPS_DIAG_CAN_FRAME_SIZE - 1U,
                                 &sample_golden) == false);

  /* per-mille 범위 초과는 세 프레임 모두에서 거부한다. 잘못된 표본이
   * 일부 프레임만 나가서 서버가 반쪽 세트를 정상으로 오인하면 안 된다. */
  sample = sample_golden;
  sample.apps1_per_mille = APPS_DIAG_CAN_PER_MILLE_MAX + 1U;
  assert(appsDiagCanEncodeRaw(data, sizeof(data), &sample) == false);
  assert(appsDiagCanEncodeScaled(data, sizeof(data), &sample) == false);
  assert(appsDiagCanEncodeStatus(data, sizeof(data), &sample) == false);

  sample = sample_golden;
  sample.apps2_per_mille = APPS_DIAG_CAN_PER_MILLE_MAX + 1U;
  assert(appsDiagCanEncodeScaled(data, sizeof(data), &sample) == false);

  sample = sample_golden;
  sample.pedal_per_mille = APPS_DIAG_CAN_PER_MILLE_MAX + 1U;
  assert(appsDiagCanEncodeRaw(data, sizeof(data), &sample) == false);

  /* 경계값은 통과해야 한다 */
  sample = sample_golden;
  sample.apps1_per_mille = APPS_DIAG_CAN_PER_MILLE_MAX;
  sample.apps2_per_mille = APPS_DIAG_CAN_PER_MILLE_MAX;
  sample.pedal_per_mille = APPS_DIAG_CAN_PER_MILLE_MAX;
  assert(appsDiagCanEncodeRaw(data, sizeof(data), &sample) == true);
  assert(appsDiagCanEncodeScaled(data, sizeof(data), &sample) == true);
}

/* 진단 ID가 제어 프레임 ID와 겹치면 토크 명령과 충돌한다. Bamocar의
 * 0x201/0x181과 분리되어 있고 우선순위가 더 낮음을 고정한다. */
static void testDiagIdsDoNotCollideWithControl(void)
{
  assert(APPS_DIAG_CAN_ID_RAW    == 0x7A0U);
  assert(APPS_DIAG_CAN_ID_SCALED == 0x7A1U);
  assert(APPS_DIAG_CAN_ID_STATUS == 0x7A2U);

  /* 표준 11비트 범위 안 */
  assert(APPS_DIAG_CAN_ID_STATUS <= 0x7FFU);

  /* 서로 달라야 한다 */
  assert(APPS_DIAG_CAN_ID_RAW != APPS_DIAG_CAN_ID_SCALED);
  assert(APPS_DIAG_CAN_ID_SCALED != APPS_DIAG_CAN_ID_STATUS);

  /* Bamocar 제어 ID와 겹치지 않고, ID가 더 커서 우선순위가 낮다 */
  assert(APPS_DIAG_CAN_ID_RAW != 0x201U);
  assert(APPS_DIAG_CAN_ID_RAW != 0x181U);
  assert(APPS_DIAG_CAN_ID_RAW > 0x201U);
}

int main(void)
{
  testGoldenRawFrame();
  testGoldenScaledFrame();
  testGoldenStatusFrame();
  testCounterIsSharedAcrossFrames();
  testStatusTruncatesToLow16Bits();
  testEndianRoundTrip();
  testArgumentAndRangeValidation();
  testDiagIdsDoNotCollideWithControl();

  puts("apps diag can tests: PASS");
  return 0;
}
