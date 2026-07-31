/*
 * display_link.c
 *
 * L432 display packet receiver.
 */


#include "display_link.h"

#include <string.h>


#define DISPLAY_LINK_FRAME_INDEX_NONE       DISPLAY_PROTOCOL_MAX_PACKET_SIZE

/* 길이 바이트(byte 4)를 읽어야 이 프레임이 몇 바이트인지 알 수 있다. 그 전까지는
 * 헤더 5바이트만 모은다. */
#define DISPLAY_LINK_HEADER_SIZE            5U


typedef struct
{
  uint8_t frame[DISPLAY_PROTOCOL_MAX_PACKET_SIZE];
  uint8_t frame_index;
  /* 0이면 아직 길이를 모른다는 뜻. 헤더를 다 받으면 타입에 맞는 값이 들어간다. */
  uint8_t frame_length;
  uint16_t last_sequence;
  bool has_sequence;
  uint16_t last_telemetry_sequence;
  bool has_telemetry_sequence;
  uint32_t last_uart_overrun_count;
  display_link_snapshot_t snapshot;
} display_link_state_t;


static display_link_state_t display_link;


#ifdef _USE_HW_CLI
static void displayLinkCli(cli_args_t *args);
#endif


static void displayLinkDisconnect(void)
{
  display_link.snapshot.connected = false;
  display_link.has_sequence = false;
  display_link.frame_index = 0U;
  display_link.frame_length = 0U;
}


static void displayLinkRecoverParser(void)
{
  uint8_t next_index = DISPLAY_LINK_FRAME_INDEX_NONE;

  /* 재동기화하면 길이는 다시 미확정이 된다. 남겨두면 다음 프레임을 엉뚱한
   * 길이로 잘라 읽는다. */
  display_link.frame_length = 0U;

  for (uint8_t i = 1U; i + 1U < display_link.frame_index; i++)
  {
    if ((display_link.frame[i] == DISPLAY_PROTOCOL_MAGIC_0) &&
        (display_link.frame[i + 1U] == DISPLAY_PROTOCOL_MAGIC_1))
    {
      next_index = i;
      break;
    }
  }

  if (next_index != DISPLAY_LINK_FRAME_INDEX_NONE)
  {
    uint8_t remaining = (uint8_t)(display_link.frame_index - next_index);

    memmove(display_link.frame,
            &display_link.frame[next_index],
            remaining);
    display_link.frame_index = remaining;
  }
  else if ((display_link.frame_index > 0U) &&
           (display_link.frame[display_link.frame_index - 1U] ==
            DISPLAY_PROTOCOL_MAGIC_0))
  {
    display_link.frame[0] = DISPLAY_PROTOCOL_MAGIC_0;
    display_link.frame_index = 1U;
  }
  else
  {
    display_link.frame_index = 0U;
  }
}

static void displayLinkAcceptTelemetry(
    const display_protocol_telemetry_t *p_telemetry)
{
  display_protocol_sequence_result_t sequence_result;
  uint16_t gap_count = 0U;

  sequence_result = displayProtocolClassifySequence(
      display_link.has_telemetry_sequence,
      display_link.last_telemetry_sequence,
      p_telemetry->sequence,
      &gap_count);

  /* 텔레메트리는 APPS 상태와 시퀀스 카운터가 독립이다. 중복/역행은 버리고
   * 누락은 세기만 한다. */
  if ((sequence_result == DISPLAY_PROTOCOL_SEQUENCE_DUPLICATE) ||
      (sequence_result == DISPLAY_PROTOCOL_SEQUENCE_STALE))
  {
    return;
  }
  if (sequence_result == DISPLAY_PROTOCOL_SEQUENCE_GAP)
  {
    display_link.snapshot.telemetry_gap_count += gap_count;
  }

  display_link.snapshot.telemetry = *p_telemetry;
  display_link.snapshot.telemetry_last_valid_ms = millis();
  display_link.snapshot.telemetry_packet_count++;
  display_link.snapshot.telemetry_fresh = true;
  display_link.snapshot.has_telemetry = true;
  display_link.last_telemetry_sequence = p_telemetry->sequence;
  display_link.has_telemetry_sequence = true;
}

static void displayLinkAcceptPacket(const display_protocol_status_t *p_status)
{
  display_protocol_sequence_result_t sequence_result;
  uint16_t gap_count = 0U;
  uint32_t now_ms;
  uint32_t valid_time_ms;

  sequence_result = displayProtocolClassifySequence(
      display_link.has_sequence,
      display_link.last_sequence,
      p_status->sequence,
      &gap_count);

  if (sequence_result == DISPLAY_PROTOCOL_SEQUENCE_DUPLICATE)
  {
    display_link.snapshot.duplicate_count++;
    return;
  }
  if (sequence_result == DISPLAY_PROTOCOL_SEQUENCE_STALE)
  {
    display_link.snapshot.stale_count++;
    return;
  }
  if (sequence_result == DISPLAY_PROTOCOL_SEQUENCE_GAP)
  {
    display_link.snapshot.sequence_gap_count += gap_count;
  }

  now_ms = millis();
  valid_time_ms = now_ms;

  /*
   * Preserve the age of packets that waited in DMA during a short main-loop
   * stall. A queued old frame must not become fresh just because it is parsed
   * now. FIRST packets re-anchor the independent L432/F411 clocks.
   */
  if (sequence_result != DISPLAY_PROTOCOL_SEQUENCE_FIRST)
  {
    uint32_t source_elapsed =
        p_status->source_time_ms -
        display_link.snapshot.status.source_time_ms;
    uint32_t local_elapsed =
        now_ms - display_link.snapshot.last_valid_ms;

    if (source_elapsed <= local_elapsed)
    {
      valid_time_ms =
          display_link.snapshot.last_valid_ms + source_elapsed;
    }
  }

  display_link.snapshot.status = *p_status;
  display_link.snapshot.last_valid_ms = valid_time_ms;
  display_link.snapshot.valid_packet_count++;
  display_link.snapshot.connected = true;
  display_link.snapshot.has_data = true;
  display_link.last_sequence = p_status->sequence;
  display_link.has_sequence = true;
}

static void displayLinkProcessFrame(void)
{
  display_protocol_result_t result;
  uint8_t type = display_link.frame[3];
  uint8_t length = display_link.frame_length;

  if (type == DISPLAY_PROTOCOL_TYPE_TELEMETRY)
  {
    display_protocol_telemetry_t telemetry;

    result = displayProtocolDecodeTelemetry(display_link.frame,
                                            length,
                                            &telemetry);
    if (result == DISPLAY_PROTOCOL_RESULT_OK)
    {
      displayLinkAcceptTelemetry(&telemetry);
      display_link.frame_index = 0U;
      display_link.frame_length = 0U;
      return;
    }
  }
  else
  {
    display_protocol_status_t status;

    result = displayProtocolDecodeStatus(display_link.frame,
                                         length,
                                         &status);
    if (result == DISPLAY_PROTOCOL_RESULT_OK)
    {
      displayLinkAcceptPacket(&status);
      display_link.frame_index = 0U;
      display_link.frame_length = 0U;
      return;
    }
  }

  if (result == DISPLAY_PROTOCOL_RESULT_CRC)
  {
    display_link.snapshot.crc_error_count++;
  }
  else
  {
    display_link.snapshot.format_error_count++;
  }
  displayLinkRecoverParser();
}

static void displayLinkPushByte(uint8_t data)
{
  if (display_link.frame_index == 0U)
  {
    if (data == DISPLAY_PROTOCOL_MAGIC_0)
    {
      display_link.frame[0] = data;
      display_link.frame_index = 1U;
      display_link.frame_length = 0U;
    }
    return;
  }

  if (display_link.frame_index == 1U)
  {
    if (data == DISPLAY_PROTOCOL_MAGIC_1)
    {
      display_link.frame[1] = data;
      display_link.frame_index = 2U;
    }
    else if (data != DISPLAY_PROTOCOL_MAGIC_0)
    {
      display_link.frame_index = 0U;
    }
    return;
  }

  display_link.frame[display_link.frame_index++] = data;

  /*
   * 헤더가 다 모이면 타입으로부터 이 프레임의 진짜 길이를 확정한다. 길이 바이트를
   * 그대로 믿지 않고 타입이 요구하는 값과 대조해야, 손상된 길이 바이트 때문에
   * 파서가 영영 오지 않을 바이트를 기다리는 상태에 빠지지 않는다.
   */
  if (display_link.frame_index == DISPLAY_LINK_HEADER_SIZE)
  {
    uint8_t expected = displayProtocolSizeForType(display_link.frame[3]);

    if ((expected == 0U) || (display_link.frame[4] != expected))
    {
      display_link.snapshot.format_error_count++;
      displayLinkRecoverParser();
      return;
    }

    display_link.frame_length = expected;
  }

  if ((display_link.frame_length != 0U) &&
      (display_link.frame_index >= display_link.frame_length))
  {
    displayLinkProcessFrame();
  }
}

bool displayLinkInit(void)
{
  bool ret;


  memset(&display_link, 0, sizeof(display_link));
  ret = uartOpen(DISPLAY_LINK_UART_CH, DISPLAY_LINK_UART_BAUD);
  display_link.last_uart_overrun_count =
      uartGetRxOverrunCount(DISPLAY_LINK_UART_CH);

#ifdef _USE_HW_CLI
  (void)cliAdd("display", displayLinkCli);
#endif

  return ret;
}

void displayLinkUpdate(void)
{
  uint32_t now_ms;
  uint32_t uart_overrun_count;

  /*
   * Drain the circular DMA buffer before checking link age. A full-screen
   * ILI9488 repaint can keep the bare-metal TouchGFX loop busy for more than
   * DISPLAY_LINK_TIMEOUT_MS, while the 512-byte UART DMA buffer can still
   * hold the packets received during that interval. Discarding those bytes
   * solely because the main loop was late creates a false DATA LOST state.
   */
  while (uartAvailable(DISPLAY_LINK_UART_CH) > 0U)
  {
    displayLinkPushByte(uartRead(DISPLAY_LINK_UART_CH));
  }

  uart_overrun_count = uartGetRxOverrunCount(DISPLAY_LINK_UART_CH);
  if (uart_overrun_count != display_link.last_uart_overrun_count)
  {
    display_link.last_uart_overrun_count = uart_overrun_count;
    display_link.snapshot.uart_overrun_count = uart_overrun_count;
    (void)uartFlush(DISPLAY_LINK_UART_CH);
    displayLinkDisconnect();
    return;
  }

  now_ms = millis();
  if ((display_link.snapshot.connected == true) &&
      ((now_ms - display_link.snapshot.last_valid_ms) >=
       DISPLAY_LINK_TIMEOUT_MS))
  {
    displayLinkDisconnect();
  }

  /* 값 자체는 남겨두고 신선도만 내린다. 표시측이 마지막 값을 계속 띄우는 대신
   * 자리표시자로 바꿀 수 있게 하기 위함이다. */
  if ((display_link.snapshot.telemetry_fresh == true) &&
      ((now_ms - display_link.snapshot.telemetry_last_valid_ms) >=
       DISPLAY_LINK_TELEMETRY_TIMEOUT_MS))
  {
    display_link.snapshot.telemetry_fresh = false;
  }
}

bool displayLinkGetSnapshot(display_link_snapshot_t *p_snapshot)
{
  if (p_snapshot == NULL)
  {
    return false;
  }

  *p_snapshot = display_link.snapshot;
  return display_link.snapshot.has_data;
}

#ifdef _USE_HW_CLI
static void displayLinkCli(cli_args_t *args)
{
  bool ret = false;


  if ((args->argc == 1) && args->isStr(0, "info"))
  {
    display_link_snapshot_t snapshot = {0};
    bool has_data;


    has_data = displayLinkGetSnapshot(&snapshot);

    cliPrintf("connected  : %d\n", snapshot.connected);
    cliPrintf("has_data   : %d\n", has_data);
    if (has_data == true)
    {
      cliPrintf("age_ms     : %lu\n",
                (unsigned long)(millis() - snapshot.last_valid_ms));
      cliPrintf("sequence   : %u\n", snapshot.status.sequence);
      cliPrintf("source_ms  : %lu\n",
                (unsigned long)snapshot.status.source_time_ms);
      cliPrintf("flags      : 0x%02X\n", snapshot.status.flags);
      cliPrintf("apps1      : raw=%u, %u.%u%%\n",
                snapshot.status.apps1_raw,
                snapshot.status.apps1_per_mille / 10U,
                snapshot.status.apps1_per_mille % 10U);
      cliPrintf("apps2      : raw=%u, %u.%u%%\n",
                snapshot.status.apps2_raw,
                snapshot.status.apps2_per_mille / 10U,
                snapshot.status.apps2_per_mille % 10U);
      cliPrintf("pedal      : %u.%u%%\n",
                snapshot.status.pedal_per_mille / 10U,
                snapshot.status.pedal_per_mille % 10U);
      cliPrintf("command    : %d\n", snapshot.status.command);
      cliPrintf("status     : 0x%08lX\n",
                (unsigned long)snapshot.status.status);
      cliPrintf("fault      : 0x%08lX\n",
                (unsigned long)snapshot.status.fault);
    }

    cliPrintf("tlm_data   : %d (fresh=%d)\n",
              snapshot.has_telemetry, snapshot.telemetry_fresh);
    if (snapshot.has_telemetry == true)
    {
      cliPrintf("tlm_age_ms : %lu\n",
                (unsigned long)(millis() -
                                snapshot.telemetry_last_valid_ms));
      cliPrintf("tlm_mask   : 0x%08lX\n",
                (unsigned long)snapshot.telemetry.valid_mask);
      cliPrintf("rpm        : %ld\n",
                (long)snapshot.telemetry.motor_rpm);
      cliPrintf("speed      : %u.%u km/h\n",
                snapshot.telemetry.road_speed_dkph / 10U,
                snapshot.telemetry.road_speed_dkph % 10U);
      cliPrintf("pack       : %u.%u V / %d.%d A\n",
                snapshot.telemetry.pack_dv / 10U,
                snapshot.telemetry.pack_dv % 10U,
                snapshot.telemetry.pack_da / 10,
                (snapshot.telemetry.pack_da < 0 ?
                 -snapshot.telemetry.pack_da : snapshot.telemetry.pack_da) % 10);
      cliPrintf("soc        : %u.%u %%\n",
                snapshot.telemetry.soc_per_mille / 10U,
                snapshot.telemetry.soc_per_mille % 10U);
      cliPrintf("cell_temp  : %d.%d C\n",
                snapshot.telemetry.cell_max_temp_decic / 10,
                (snapshot.telemetry.cell_max_temp_decic < 0 ?
                 -snapshot.telemetry.cell_max_temp_decic :
                 snapshot.telemetry.cell_max_temp_decic) % 10);
      cliPrintf("igbt_temp  : %d.%d C\n",
                snapshot.telemetry.igbt_temp_decic / 10,
                (snapshot.telemetry.igbt_temp_decic < 0 ?
                 -snapshot.telemetry.igbt_temp_decic :
                 snapshot.telemetry.igbt_temp_decic) % 10);
      cliPrintf("tlm_count  : %lu (gap %lu)\n",
                (unsigned long)snapshot.telemetry_packet_count,
                (unsigned long)snapshot.telemetry_gap_count);
    }

    cliPrintf("valid      : %lu\n",
              (unsigned long)snapshot.valid_packet_count);
    cliPrintf("crc_error  : %lu\n",
              (unsigned long)snapshot.crc_error_count);
    cliPrintf("format_err : %lu\n",
              (unsigned long)snapshot.format_error_count);
    cliPrintf("seq_gap    : %lu\n",
              (unsigned long)snapshot.sequence_gap_count);
    cliPrintf("duplicate  : %lu\n",
              (unsigned long)snapshot.duplicate_count);
    cliPrintf("stale      : %lu\n",
              (unsigned long)snapshot.stale_count);
    cliPrintf("uart_ovr   : %lu\n",
              (unsigned long)snapshot.uart_overrun_count);

    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("display info\n");
  }
}
#endif
