#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../shared/display_protocol/display_protocol.h"


static void testCrcKnownVector(void)
{
  static const uint8_t input[] = "123456789";

  assert(displayProtocolCrc16(input, sizeof(input) - 1U) == 0x29B1U);
}

static void testGoldenPacket(void)
{
  static const uint8_t expected[DISPLAY_PROTOCOL_PACKET_SIZE] =
  {
    0x4CU, 0x4FU, 0x01U, 0x01U, 0x22U, 0x07U, 0x34U, 0x12U,
    0xEFU, 0xCDU, 0xABU, 0x89U, 0xE8U, 0x03U, 0xD0U, 0x07U,
    0xFAU, 0x00U, 0xFFU, 0x00U, 0xFCU, 0x00U, 0x2EU, 0xFBU,
    0x10U, 0x00U, 0x00U, 0x00U, 0x30U, 0x00U, 0x00U, 0x00U,
    0x94U, 0x9EU,
  };
  const display_protocol_status_t status =
  {
    .sequence = 0x1234U,
    .flags = DISPLAY_PROTOCOL_FLAG_VALID |
             DISPLAY_PROTOCOL_FLAG_CONFIGURED |
             DISPLAY_PROTOCOL_FLAG_ADC_OK,
    .source_time_ms = 0x89ABCDEFU,
    .apps1_raw = 1000U,
    .apps2_raw = 2000U,
    .apps1_per_mille = 250U,
    .apps2_per_mille = 255U,
    .pedal_per_mille = 252U,
    .command = -1234,
    .status = 0x00000010U,
    .fault = 0x00000030U,
  };
  uint8_t packet[DISPLAY_PROTOCOL_PACKET_SIZE];

  assert(displayProtocolEncodeStatus(packet, sizeof(packet), &status));
  assert(memcmp(packet, expected, sizeof(expected)) == 0);
}

static void testRoundTripAndCorruption(void)
{
  display_protocol_status_t input =
  {
    .sequence = 0x1234U,
    .flags = DISPLAY_PROTOCOL_FLAG_VALID |
             DISPLAY_PROTOCOL_FLAG_CONFIGURED |
             DISPLAY_PROTOCOL_FLAG_ADC_OK,
    .source_time_ms = 0x89ABCDEFU,
    .apps1_raw = 1000U,
    .apps2_raw = 2000U,
    .apps1_per_mille = 250U,
    .apps2_per_mille = 255U,
    .pedal_per_mille = 252U,
    .command = -1234,
    .status = 0x00000010U,
    .fault = 0x00000030U,
  };
  display_protocol_status_t output = {0};
  uint8_t packet[DISPLAY_PROTOCOL_PACKET_SIZE];

  assert(displayProtocolEncodeStatus(packet, sizeof(packet), &input));
  assert(displayProtocolDecodeStatus(packet, sizeof(packet), &output) ==
         DISPLAY_PROTOCOL_RESULT_OK);
  assert(output.sequence == input.sequence);
  assert(output.flags == input.flags);
  assert(output.source_time_ms == input.source_time_ms);
  assert(output.apps1_raw == input.apps1_raw);
  assert(output.apps2_raw == input.apps2_raw);
  assert(output.apps1_per_mille == input.apps1_per_mille);
  assert(output.apps2_per_mille == input.apps2_per_mille);
  assert(output.pedal_per_mille == input.pedal_per_mille);
  assert(output.command == input.command);
  assert(output.status == input.status);
  assert(output.fault == input.fault);

  for (size_t i = 0U; i < sizeof(packet); i++)
  {
    for (uint8_t bit = 0U; bit < 8U; bit++)
    {
      uint8_t saved = packet[i];

      packet[i] ^= (uint8_t)(1U << bit);
      assert(displayProtocolDecodeStatus(packet, sizeof(packet), &output) !=
             DISPLAY_PROTOCOL_RESULT_OK);
      packet[i] = saved;
    }
  }
}

static void testHeaderAndRangeValidation(void)
{
  display_protocol_status_t status = {0};
  uint8_t packet[DISPLAY_PROTOCOL_PACKET_SIZE];

  assert(displayProtocolEncodeStatus(packet, sizeof(packet), &status));

  packet[0] = 0U;
  assert(displayProtocolDecodeStatus(packet, sizeof(packet), &status) ==
         DISPLAY_PROTOCOL_RESULT_MAGIC);
  packet[0] = DISPLAY_PROTOCOL_MAGIC_0;

  packet[2]++;
  assert(displayProtocolDecodeStatus(packet, sizeof(packet), &status) ==
         DISPLAY_PROTOCOL_RESULT_VERSION);
  packet[2] = DISPLAY_PROTOCOL_VERSION;

  assert(displayProtocolDecodeStatus(packet, sizeof(packet) - 1U, &status) ==
         DISPLAY_PROTOCOL_RESULT_LENGTH);
  assert(displayProtocolDecodeStatus(packet, sizeof(packet) + 1U, &status) ==
         DISPLAY_PROTOCOL_RESULT_LENGTH);

  status.pedal_per_mille = DISPLAY_PROTOCOL_PER_MILLE_MAX + 1U;
  assert(displayProtocolEncodeStatus(packet, sizeof(packet), &status) == false);

  status.pedal_per_mille = 0U;
  status.apps1_per_mille = DISPLAY_PROTOCOL_PER_MILLE_MAX + 1U;
  assert(displayProtocolEncodeStatus(packet, sizeof(packet), &status) == false);

  status.apps1_per_mille = 0U;
  status.apps2_per_mille = DISPLAY_PROTOCOL_PER_MILLE_MAX + 1U;
  assert(displayProtocolEncodeStatus(packet, sizeof(packet), &status) == false);
}

static void testSequenceRules(void)
{
  uint16_t gap = 0U;

  assert(displayProtocolClassifySequence(false, 0U, 77U, &gap) ==
         DISPLAY_PROTOCOL_SEQUENCE_FIRST);
  assert(displayProtocolClassifySequence(true, 77U, 78U, &gap) ==
         DISPLAY_PROTOCOL_SEQUENCE_NEXT);
  assert(displayProtocolClassifySequence(true, 77U, 80U, &gap) ==
         DISPLAY_PROTOCOL_SEQUENCE_GAP);
  assert(gap == 2U);
  assert(displayProtocolClassifySequence(true, 77U, 77U, &gap) ==
         DISPLAY_PROTOCOL_SEQUENCE_DUPLICATE);
  assert(displayProtocolClassifySequence(true, 77U, 76U, &gap) ==
         DISPLAY_PROTOCOL_SEQUENCE_STALE);
  assert(displayProtocolClassifySequence(true, 0xFFFFU, 0U, &gap) ==
         DISPLAY_PROTOCOL_SEQUENCE_NEXT);
  assert(displayProtocolClassifySequence(true, 0U, 0x7FFFU, &gap) ==
         DISPLAY_PROTOCOL_SEQUENCE_GAP);
  assert(gap == 0x7FFEU);
  assert(displayProtocolClassifySequence(true, 0U, 0x8000U, &gap) ==
         DISPLAY_PROTOCOL_SEQUENCE_STALE);
  assert(displayProtocolClassifySequence(true, 0xFFFEU, 1U, &gap) ==
         DISPLAY_PROTOCOL_SEQUENCE_GAP);
  assert(gap == 2U);
}

int main(void)
{
  testCrcKnownVector();
  testGoldenPacket();
  testRoundTripAndCorruption();
  testHeaderAndRangeValidation();
  testSequenceRules();

  puts("display protocol tests: PASS");
  return 0;
}
