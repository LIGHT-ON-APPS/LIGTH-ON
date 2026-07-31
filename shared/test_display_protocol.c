#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "display_protocol.h"


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

static const display_protocol_telemetry_t telemetry_reference =
{
  .sequence = 0x1234U,
  .source_time_ms = 0x89ABCDEFU,
  .valid_mask = DISPLAY_PROTOCOL_TLM_PACK_V | DISPLAY_PROTOCOL_TLM_PACK_A |
                DISPLAY_PROTOCOL_TLM_SOC | DISPLAY_PROTOCOL_TLM_CELL_TEMP |
                DISPLAY_PROTOCOL_TLM_RPM | DISPLAY_PROTOCOL_TLM_IGBT_TEMP,
  .motor_rpm = 4820,
  .road_speed_dkph = 0U,
  .pack_dv = 3962U,
  .pack_da = 1523,
  .soc_per_mille = 784U,
  .cell_max_temp_decic = 421,
  .igbt_temp_decic = 513,
  .motor_temp_decic = -105,
};

static void testTelemetryGoldenPacket(void)
{
  static const uint8_t expected[DISPLAY_PROTOCOL_TELEMETRY_SIZE] =
  {
    0x4CU, 0x4FU, 0x01U, 0x02U, 0x24U, 0x00U, 0x34U, 0x12U,
    0xEFU, 0xCDU, 0xABU, 0x89U, 0x2FU, 0x02U, 0x00U, 0x00U,
    0xD4U, 0x12U, 0x00U, 0x00U, 0x00U, 0x00U, 0x7AU, 0x0FU,
    0xF3U, 0x05U, 0x10U, 0x03U, 0xA5U, 0x01U, 0x01U, 0x02U,
    0x97U, 0xFFU, 0x01U, 0xA9U,
  };
  uint8_t packet[DISPLAY_PROTOCOL_TELEMETRY_SIZE];

  assert(displayProtocolEncodeTelemetry(packet,
                                        sizeof(packet),
                                        &telemetry_reference));
  assert(memcmp(packet, expected, sizeof(expected)) == 0);
}

static void testTelemetryRoundTrip(void)
{
  uint8_t packet[DISPLAY_PROTOCOL_TELEMETRY_SIZE];
  display_protocol_telemetry_t output;

  assert(displayProtocolEncodeTelemetry(packet,
                                        sizeof(packet),
                                        &telemetry_reference));
  assert(displayProtocolDecodeTelemetry(packet, sizeof(packet), &output) ==
         DISPLAY_PROTOCOL_RESULT_OK);

  assert(output.sequence == telemetry_reference.sequence);
  assert(output.source_time_ms == telemetry_reference.source_time_ms);
  assert(output.valid_mask == telemetry_reference.valid_mask);
  assert(output.motor_rpm == telemetry_reference.motor_rpm);
  assert(output.road_speed_dkph == telemetry_reference.road_speed_dkph);
  assert(output.pack_dv == telemetry_reference.pack_dv);
  assert(output.pack_da == telemetry_reference.pack_da);
  assert(output.soc_per_mille == telemetry_reference.soc_per_mille);
  assert(output.cell_max_temp_decic ==
         telemetry_reference.cell_max_temp_decic);
  assert(output.igbt_temp_decic == telemetry_reference.igbt_temp_decic);
  /* Sub-zero temperatures must survive as negatives, not wrap to ~65000. */
  assert(output.motor_temp_decic == -105);

  /* Every single-byte corruption has to be caught by the CRC. */
  for (size_t i = 0U; i < sizeof(packet); i++)
  {
    uint8_t saved = packet[i];

    packet[i] = (uint8_t)(saved ^ 0xFFU);
    assert(displayProtocolDecodeTelemetry(packet, sizeof(packet), &output) !=
           DISPLAY_PROTOCOL_RESULT_OK);
    packet[i] = saved;
  }
}

static void testTelemetryNegativeCurrentAndExtremes(void)
{
  display_protocol_telemetry_t input = telemetry_reference;
  display_protocol_telemetry_t output;
  uint8_t packet[DISPLAY_PROTOCOL_TELEMETRY_SIZE];

  /* Regeneration (charging) reverses the pack current sign, and the motor can
   * spin backwards. Both must round-trip. */
  input.pack_da = -2500;
  input.motor_rpm = -6500;
  input.cell_max_temp_decic = -150;
  input.soc_per_mille = DISPLAY_PROTOCOL_PER_MILLE_MAX;

  assert(displayProtocolEncodeTelemetry(packet, sizeof(packet), &input));
  assert(displayProtocolDecodeTelemetry(packet, sizeof(packet), &output) ==
         DISPLAY_PROTOCOL_RESULT_OK);
  assert(output.pack_da == -2500);
  assert(output.motor_rpm == -6500);
  assert(output.cell_max_temp_decic == -150);
  assert(output.soc_per_mille == DISPLAY_PROTOCOL_PER_MILLE_MAX);

  /* Out-of-range SOC is refused on the way out and on the way in. */
  input.soc_per_mille = DISPLAY_PROTOCOL_PER_MILLE_MAX + 1U;
  assert(displayProtocolEncodeTelemetry(packet, sizeof(packet), &input) ==
         false);
}

static void testTypeDispatch(void)
{
  uint8_t status_packet[DISPLAY_PROTOCOL_PACKET_SIZE];
  uint8_t telemetry_packet[DISPLAY_PROTOCOL_TELEMETRY_SIZE];
  const display_protocol_status_t status =
  {
    .sequence = 1U,
    .flags = DISPLAY_PROTOCOL_FLAG_VALID,
    .source_time_ms = 10U,
    .apps1_raw = 1U,
    .apps2_raw = 2U,
    .apps1_per_mille = 3U,
    .apps2_per_mille = 4U,
    .pedal_per_mille = 5U,
    .command = 6,
    .status = 0U,
    .fault = 0U,
  };
  display_protocol_status_t status_out;
  display_protocol_telemetry_t telemetry_out;

  assert(displayProtocolSizeForType(DISPLAY_PROTOCOL_TYPE_APPS_STATUS) ==
         DISPLAY_PROTOCOL_PACKET_SIZE);
  assert(displayProtocolSizeForType(DISPLAY_PROTOCOL_TYPE_TELEMETRY) ==
         DISPLAY_PROTOCOL_TELEMETRY_SIZE);
  /* Unknown types report 0 so a receiver never waits for a phantom packet. */
  assert(displayProtocolSizeForType(0U) == 0U);
  assert(displayProtocolSizeForType(3U) == 0U);
  assert(displayProtocolSizeForType(255U) == 0U);

  assert(displayProtocolEncodeStatus(status_packet,
                                     sizeof(status_packet),
                                     &status));
  assert(displayProtocolEncodeTelemetry(telemetry_packet,
                                        sizeof(telemetry_packet),
                                        &telemetry_reference));

  /* Feeding a packet to the wrong decoder must fail on length or type, never
   * silently reinterpret the payload as the other message. */
  assert(displayProtocolDecodeTelemetry(status_packet,
                                        sizeof(status_packet),
                                        &telemetry_out) !=
         DISPLAY_PROTOCOL_RESULT_OK);
  assert(displayProtocolDecodeStatus(telemetry_packet,
                                     sizeof(telemetry_packet),
                                     &status_out) !=
         DISPLAY_PROTOCOL_RESULT_OK);
}

int main(void)
{
  testCrcKnownVector();
  testGoldenPacket();
  testRoundTripAndCorruption();
  testHeaderAndRangeValidation();
  testSequenceRules();
  testTelemetryGoldenPacket();
  testTelemetryRoundTrip();
  testTelemetryNegativeCurrentAndExtremes();
  testTypeDispatch();

  puts("display protocol tests: PASS");
  return 0;
}
