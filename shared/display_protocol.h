#ifndef SHARED_DISPLAY_PROTOCOL_H_
#define SHARED_DISPLAY_PROTOCOL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


/*
 * L432 -> F411 display status packet, protocol version 1.
 *
 * All multi-byte fields are little-endian. Never transmit a C struct directly.
 * CRC-16/CCITT-FALSE covers bytes [0, DISPLAY_PROTOCOL_CRC_OFFSET).
 *
 *  0  magic[0]             0x4C ('L')
 *  1  magic[1]             0x4F ('O')
 *  2  protocol version     1
 *  3  message type         1 (APPS status)
 *  4  total packet length  34
 *  5  state flags
 *  6  sequence             uint16
 *  8  source time          uint32 milliseconds
 * 12  APPS1 raw            uint16 ADC count
 * 14  APPS2 raw            uint16 ADC count
 * 16  APPS1 position       uint16 per-mille
 * 18  APPS2 position       uint16 per-mille
 * 20  final pedal          uint16 per-mille
 * 22  sent command         int16
 * 24  current status       uint32 bit mask
 * 28  latched fault        uint32 bit mask
 * 32  CRC16                uint16
 */

#define DISPLAY_PROTOCOL_MAGIC_0                  0x4CU
#define DISPLAY_PROTOCOL_MAGIC_1                  0x4FU
#define DISPLAY_PROTOCOL_VERSION                  1U
#define DISPLAY_PROTOCOL_TYPE_APPS_STATUS         1U
#define DISPLAY_PROTOCOL_PACKET_SIZE              34U
#define DISPLAY_PROTOCOL_CRC_OFFSET               32U
#define DISPLAY_PROTOCOL_PER_MILLE_MAX            1000U

#define DISPLAY_PROTOCOL_FLAG_VALID               (1U << 0)
#define DISPLAY_PROTOCOL_FLAG_CONFIGURED          (1U << 1)
#define DISPLAY_PROTOCOL_FLAG_ADC_OK              (1U << 2)
#define DISPLAY_PROTOCOL_FLAG_CAN_TX_OK           (1U << 3)
#define DISPLAY_PROTOCOL_FLAG_CAN_TX_MUTED        (1U << 4)
#define DISPLAY_PROTOCOL_FLAG_FAULT_LATCHED       (1U << 5)


typedef struct
{
  uint16_t sequence;
  uint8_t  flags;
  uint32_t source_time_ms;
  uint16_t apps1_raw;
  uint16_t apps2_raw;
  uint16_t apps1_per_mille;
  uint16_t apps2_per_mille;
  uint16_t pedal_per_mille;
  int16_t  command;
  uint32_t status;
  uint32_t fault;
} display_protocol_status_t;

typedef enum
{
  DISPLAY_PROTOCOL_RESULT_OK = 0,
  DISPLAY_PROTOCOL_RESULT_ARGUMENT,
  DISPLAY_PROTOCOL_RESULT_LENGTH,
  DISPLAY_PROTOCOL_RESULT_MAGIC,
  DISPLAY_PROTOCOL_RESULT_VERSION,
  DISPLAY_PROTOCOL_RESULT_TYPE,
  DISPLAY_PROTOCOL_RESULT_CRC,
  DISPLAY_PROTOCOL_RESULT_RANGE,
} display_protocol_result_t;

typedef enum
{
  DISPLAY_PROTOCOL_SEQUENCE_FIRST = 0,
  DISPLAY_PROTOCOL_SEQUENCE_NEXT,
  DISPLAY_PROTOCOL_SEQUENCE_GAP,
  DISPLAY_PROTOCOL_SEQUENCE_DUPLICATE,
  DISPLAY_PROTOCOL_SEQUENCE_STALE,
} display_protocol_sequence_result_t;


static inline void displayProtocolPutU16Le(uint8_t *p_data, uint16_t value)
{
  p_data[0] = (uint8_t)(value & 0xFFU);
  p_data[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static inline void displayProtocolPutU32Le(uint8_t *p_data, uint32_t value)
{
  p_data[0] = (uint8_t)(value & 0xFFU);
  p_data[1] = (uint8_t)((value >> 8) & 0xFFU);
  p_data[2] = (uint8_t)((value >> 16) & 0xFFU);
  p_data[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static inline uint16_t displayProtocolGetU16Le(const uint8_t *p_data)
{
  return (uint16_t)((uint16_t)p_data[0] |
                    ((uint16_t)p_data[1] << 8));
}

static inline uint32_t displayProtocolGetU32Le(const uint8_t *p_data)
{
  return (uint32_t)p_data[0] |
         ((uint32_t)p_data[1] << 8) |
         ((uint32_t)p_data[2] << 16) |
         ((uint32_t)p_data[3] << 24);
}

static inline uint16_t displayProtocolCrc16(const uint8_t *p_data, size_t length)
{
  uint16_t crc = 0xFFFFU;

  if ((p_data == NULL) && (length > 0U))
  {
    return 0U;
  }

  for (size_t i = 0U; i < length; i++)
  {
    crc ^= (uint16_t)((uint16_t)p_data[i] << 8);

    for (uint8_t bit = 0U; bit < 8U; bit++)
    {
      if ((crc & 0x8000U) != 0U)
      {
        crc = (uint16_t)((crc << 1) ^ 0x1021U);
      }
      else
      {
        crc = (uint16_t)(crc << 1);
      }
    }
  }

  return crc;
}

static inline bool displayProtocolEncodeStatus(
    uint8_t *p_packet,
    size_t packet_size,
    const display_protocol_status_t *p_status)
{
  uint16_t crc;

  if ((p_packet == NULL) ||
      (p_status == NULL) ||
      (packet_size < DISPLAY_PROTOCOL_PACKET_SIZE) ||
      (p_status->apps1_per_mille > DISPLAY_PROTOCOL_PER_MILLE_MAX) ||
      (p_status->apps2_per_mille > DISPLAY_PROTOCOL_PER_MILLE_MAX) ||
      (p_status->pedal_per_mille > DISPLAY_PROTOCOL_PER_MILLE_MAX))
  {
    return false;
  }

  p_packet[0] = DISPLAY_PROTOCOL_MAGIC_0;
  p_packet[1] = DISPLAY_PROTOCOL_MAGIC_1;
  p_packet[2] = DISPLAY_PROTOCOL_VERSION;
  p_packet[3] = DISPLAY_PROTOCOL_TYPE_APPS_STATUS;
  p_packet[4] = DISPLAY_PROTOCOL_PACKET_SIZE;
  p_packet[5] = p_status->flags;
  displayProtocolPutU16Le(&p_packet[6], p_status->sequence);
  displayProtocolPutU32Le(&p_packet[8], p_status->source_time_ms);
  displayProtocolPutU16Le(&p_packet[12], p_status->apps1_raw);
  displayProtocolPutU16Le(&p_packet[14], p_status->apps2_raw);
  displayProtocolPutU16Le(&p_packet[16], p_status->apps1_per_mille);
  displayProtocolPutU16Le(&p_packet[18], p_status->apps2_per_mille);
  displayProtocolPutU16Le(&p_packet[20], p_status->pedal_per_mille);
  displayProtocolPutU16Le(&p_packet[22], (uint16_t)p_status->command);
  displayProtocolPutU32Le(&p_packet[24], p_status->status);
  displayProtocolPutU32Le(&p_packet[28], p_status->fault);

  crc = displayProtocolCrc16(p_packet, DISPLAY_PROTOCOL_CRC_OFFSET);
  displayProtocolPutU16Le(&p_packet[DISPLAY_PROTOCOL_CRC_OFFSET], crc);

  return true;
}

static inline display_protocol_result_t displayProtocolDecodeStatus(
    const uint8_t *p_packet,
    size_t packet_size,
    display_protocol_status_t *p_status)
{
  uint16_t received_crc;
  uint16_t calculated_crc;

  if ((p_packet == NULL) || (p_status == NULL))
  {
    return DISPLAY_PROTOCOL_RESULT_ARGUMENT;
  }
  if ((packet_size != DISPLAY_PROTOCOL_PACKET_SIZE) ||
      (p_packet[4] != DISPLAY_PROTOCOL_PACKET_SIZE))
  {
    return DISPLAY_PROTOCOL_RESULT_LENGTH;
  }
  if ((p_packet[0] != DISPLAY_PROTOCOL_MAGIC_0) ||
      (p_packet[1] != DISPLAY_PROTOCOL_MAGIC_1))
  {
    return DISPLAY_PROTOCOL_RESULT_MAGIC;
  }
  if (p_packet[2] != DISPLAY_PROTOCOL_VERSION)
  {
    return DISPLAY_PROTOCOL_RESULT_VERSION;
  }
  if (p_packet[3] != DISPLAY_PROTOCOL_TYPE_APPS_STATUS)
  {
    return DISPLAY_PROTOCOL_RESULT_TYPE;
  }

  received_crc = displayProtocolGetU16Le(
      &p_packet[DISPLAY_PROTOCOL_CRC_OFFSET]);
  calculated_crc = displayProtocolCrc16(
      p_packet,
      DISPLAY_PROTOCOL_CRC_OFFSET);
  if (received_crc != calculated_crc)
  {
    return DISPLAY_PROTOCOL_RESULT_CRC;
  }

  p_status->flags = p_packet[5];
  p_status->sequence = displayProtocolGetU16Le(&p_packet[6]);
  p_status->source_time_ms = displayProtocolGetU32Le(&p_packet[8]);
  p_status->apps1_raw = displayProtocolGetU16Le(&p_packet[12]);
  p_status->apps2_raw = displayProtocolGetU16Le(&p_packet[14]);
  p_status->apps1_per_mille = displayProtocolGetU16Le(&p_packet[16]);
  p_status->apps2_per_mille = displayProtocolGetU16Le(&p_packet[18]);
  p_status->pedal_per_mille = displayProtocolGetU16Le(&p_packet[20]);
  p_status->command = (int16_t)displayProtocolGetU16Le(&p_packet[22]);
  p_status->status = displayProtocolGetU32Le(&p_packet[24]);
  p_status->fault = displayProtocolGetU32Le(&p_packet[28]);

  if ((p_status->apps1_per_mille > DISPLAY_PROTOCOL_PER_MILLE_MAX) ||
      (p_status->apps2_per_mille > DISPLAY_PROTOCOL_PER_MILLE_MAX) ||
      (p_status->pedal_per_mille > DISPLAY_PROTOCOL_PER_MILLE_MAX))
  {
    return DISPLAY_PROTOCOL_RESULT_RANGE;
  }

  return DISPLAY_PROTOCOL_RESULT_OK;
}

static inline display_protocol_sequence_result_t
displayProtocolClassifySequence(bool has_previous,
                                uint16_t previous,
                                uint16_t current,
                                uint16_t *p_gap_count)
{
  uint16_t delta;

  if (p_gap_count != NULL)
  {
    *p_gap_count = 0U;
  }
  if (has_previous != true)
  {
    return DISPLAY_PROTOCOL_SEQUENCE_FIRST;
  }

  delta = (uint16_t)(current - previous);
  if (delta == 0U)
  {
    return DISPLAY_PROTOCOL_SEQUENCE_DUPLICATE;
  }
  if (delta == 1U)
  {
    return DISPLAY_PROTOCOL_SEQUENCE_NEXT;
  }
  if (delta < 0x8000U)
  {
    if (p_gap_count != NULL)
    {
      *p_gap_count = (uint16_t)(delta - 1U);
    }
    return DISPLAY_PROTOCOL_SEQUENCE_GAP;
  }

  return DISPLAY_PROTOCOL_SEQUENCE_STALE;
}


#ifdef __cplusplus
}
#endif

#endif /* SHARED_DISPLAY_PROTOCOL_H_ */
