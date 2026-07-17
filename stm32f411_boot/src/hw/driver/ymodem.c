/*
 * ymodem.c
 *
 *  Created on: Jan 6, 2026
 *      Author: TEMP
 */


#include "ymodem.h"
#include "uart.h"
#include "cli.h"


#define YMODEM_SOH    0x01
#define YMODEM_STX    0x02
#define YMODEM_ACK    0x06
#define YMODEM_NACK   0x15
#define YMODEM_EOT    0x04
#define YMODEM_C      0x43
#define YMODEM_CAN    0x18
#define YMODEM_BS     0x08



enum
{
  YMODEM_STATE_WAIT_HEAD,
  YMODEM_STATE_WAIT_FIRST,
  YMODEM_STATE_WAIT_DATA,
  YMODEM_STATE_WAIT_LAST,
  YMODEM_STATE_WAIT_END,
  YMODEM_STATE_WAIT_CANCEL,
};

enum
{
  YMODEM_PACKET_WAIT_FIRST,
  YMODEM_PACKET_WAIT_SEQ1,
  YMODEM_PACKET_WAIT_SEQ2,
  YMODEM_PACKET_WAIT_DATA,
  YMODEM_PACKET_WAIT_CRCH,
  YMODEM_PACKET_WAIT_CRCL,
};



#ifdef _USE_HW_CLI
static void cliYmodem(cli_args_t *args);
#endif


static uint16_t crc16(uint8_t *data, uint16_t size);
static uint16_t crc_update(uint16_t crc_in, int incr);
static bool ymodemReceivePacket(ymodem_packet_t *p_packet, uint8_t data_in);




bool ymodemInit(void)
{
#ifdef _USE_HW_CLI
  cliAdd("ymodem", cliYmodem);
#endif

  return true;
}

bool ymodemOpen(ymodem_t *p_modem, uint8_t ch)
{
  if ((p_modem == NULL) || (ch >= HW_UART_MAX_CH))
  {
    return false;
  }

  memset(p_modem, 0, sizeof(ymodem_t));
  p_modem->ch = ch;
  p_modem->is_init = true;

  p_modem->state           = YMODEM_STATE_WAIT_HEAD;
  p_modem->rx_packet.state = YMODEM_PACKET_WAIT_FIRST;
  p_modem->file_buf        = &p_modem->rx_packet.buffer[3];
  p_modem->file_buf_length = 0;
  p_modem->pre_time        = millis();
  p_modem->start_time      = 3000;

  p_modem->rx_packet.data = &p_modem->rx_packet.buffer[3];

  return true;
}

bool ymodemPutch(ymodem_t *p_modem, uint8_t data)
{
  if ((p_modem == NULL) || (p_modem->is_init != true))
  {
    return false;
  }

  return uartWrite(p_modem->ch, &data, 1);
}

bool ymodemGetFileInfo(ymodem_t *p_modem)
{
  bool name_valid = false;
  bool size_valid = false;
  uint16_t size_i = 0U;
  uint32_t file_length = 0U;

  if (p_modem == NULL)
  {
    return false;
  }

  memset(p_modem->file_name, 0, sizeof(p_modem->file_name));
  p_modem->file_length = 0U;

  for (uint16_t i = 0U; i < sizeof(p_modem->file_name); i++)
  {
    p_modem->file_name[i] = p_modem->rx_packet.data[i];
    if (p_modem->file_name[i] == 0x00)
    {
      size_i = i + 1;
      name_valid = (i > 0U);
      break;
    }
  }

  if (name_valid != true)
  {
    return false;
  }

  for (uint16_t i = size_i; i < 128U; i++)
  {
    uint8_t data = p_modem->rx_packet.data[i];

    if ((data == 0x00U) || (data == 0x20U))
    {
      break;
    }

    if ((data < '0') || (data > '9'))
    {
      return false;
    }

    data = (uint8_t)(data - '0');
    if (file_length > ((UINT32_MAX - data) / 10U))
    {
      return false;
    }

    file_length = (file_length * 10U) + data;
    size_valid = true;
  }

  if ((size_valid != true) || (file_length == 0U))
  {
    return false;
  }

  p_modem->file_length = file_length;
  return true;
}

bool ymodemAck(ymodem_t *p_modem)
{
  bool ret = false;

  if ((p_modem == NULL) || (p_modem->is_init != true))
  {
    return false;
  }

  if (p_modem->state == YMODEM_STATE_WAIT_FIRST)
  {
    if (p_modem->type == YMODEM_TYPE_START)
    {
      ret = ymodemPutch(p_modem, YMODEM_ACK);
      ret = ymodemPutch(p_modem, YMODEM_C) && ret;
    }
  }

  if (p_modem->state == YMODEM_STATE_WAIT_DATA)
  {
    if (p_modem->type == YMODEM_TYPE_DATA)
    {
      ret = ymodemPutch(p_modem, YMODEM_ACK);
    }
  }

  if (p_modem->state == YMODEM_STATE_WAIT_END)
  {
    if (p_modem->type == YMODEM_TYPE_END)
    {
      ret = ymodemPutch(p_modem, YMODEM_ACK);
      if (ret == true)
      {
        p_modem->state = YMODEM_STATE_WAIT_HEAD;
      }
    }
  }

  return ret;
}

bool ymodemCancel(ymodem_t *p_modem)
{
  bool ret;

  if ((p_modem == NULL) || (p_modem->is_init != true))
  {
    return false;
  }

  ret = ymodemPutch(p_modem, YMODEM_CAN);
  ret = ymodemPutch(p_modem, YMODEM_CAN) && ret;
  p_modem->state = YMODEM_STATE_WAIT_HEAD;

  return ret;
}

bool ymodemReceive(ymodem_t *p_modem)
{
  bool ret = false;
  bool update = false;
  uint32_t buf_length;


  if (p_modem == NULL)
  {
    return false;
  }

  if (p_modem->is_init != true)
  {
    p_modem->type = YMODEM_TYPE_ERROR;
    return true;
  }

  if (uartAvailable(p_modem->ch) > 0)
  {
    p_modem->rx_data = uartRead(p_modem->ch);
    update = true;
    p_modem->pre_time = millis();

    //uartPrintf(_DEF_UART1, "Rx 0x%X, %d\n", p_modem->rx_data, p_modem->rx_packet.state);
  }

  if (update == true && ymodemReceivePacket(&p_modem->rx_packet, p_modem->rx_data) == true)
  {
    //uartPrintf(_DEF_UART1, "RxPacket 0x%X\n", p_modem->rx_packet.stx);

    if (p_modem->state != YMODEM_STATE_WAIT_HEAD)
    {
      if (p_modem->rx_packet.stx == YMODEM_CAN)
      {
        p_modem->state = YMODEM_STATE_WAIT_CANCEL;
      }
    }

    switch(p_modem->state)
    {
      case YMODEM_STATE_WAIT_HEAD:
        if (p_modem->rx_packet.stx == YMODEM_EOT)
        {
          ymodemPutch(p_modem, YMODEM_NACK);
          p_modem->state = YMODEM_STATE_WAIT_LAST;
        }
        else if (((p_modem->rx_packet.stx == YMODEM_SOH) ||
                  (p_modem->rx_packet.stx == YMODEM_STX)) &&
                 (p_modem->rx_packet.seq[0] == 0x00U))
        {
          p_modem->file_addr = 0U;
          p_modem->file_received = 0U;
          p_modem->file_buf_length = 0U;
          p_modem->index = 1U;

          if (ymodemGetFileInfo(p_modem) == true)
          {
            p_modem->state = YMODEM_STATE_WAIT_FIRST;
            p_modem->type = YMODEM_TYPE_START;
          }
          else
          {
            p_modem->type = YMODEM_TYPE_ERROR;
          }
          ret = true;
        }
        break;

      case YMODEM_STATE_WAIT_FIRST:
        if (p_modem->rx_packet.stx == YMODEM_EOT)
        {
          ymodemPutch(p_modem, YMODEM_NACK);
          p_modem->state = YMODEM_STATE_WAIT_LAST;
        }
        else if (((p_modem->rx_packet.stx == YMODEM_SOH) ||
                  (p_modem->rx_packet.stx == YMODEM_STX)) &&
                 (p_modem->rx_packet.seq[0] == 0x00U))
        {
          ymodemPutch(p_modem, YMODEM_ACK);
          ymodemPutch(p_modem, YMODEM_C);
        }
        else if (((p_modem->rx_packet.stx == YMODEM_SOH) ||
                  (p_modem->rx_packet.stx == YMODEM_STX)) &&
                 (p_modem->rx_packet.seq[0] == (uint8_t)p_modem->index))
        {
          p_modem->file_addr = p_modem->file_received;

          buf_length = p_modem->file_length - p_modem->file_addr;
          if (buf_length > p_modem->rx_packet.length)
          {
            buf_length = p_modem->rx_packet.length;
          }
          p_modem->file_buf_length = buf_length;
          p_modem->file_received += buf_length;
          p_modem->index = (uint8_t)(p_modem->index + 1U);

          p_modem->state = YMODEM_STATE_WAIT_DATA;
          p_modem->type = YMODEM_TYPE_DATA;
          ret = true;
        }
        else
        {
          ymodemPutch(p_modem, YMODEM_NACK);
        }
        break;

      case YMODEM_STATE_WAIT_DATA:
        if (p_modem->rx_packet.stx == YMODEM_EOT)
        {
          ymodemPutch(p_modem, YMODEM_NACK);
          p_modem->state = YMODEM_STATE_WAIT_LAST;
        }
        else if (((p_modem->rx_packet.stx == YMODEM_SOH) ||
                  (p_modem->rx_packet.stx == YMODEM_STX)) &&
                 (p_modem->rx_packet.seq[0] == (uint8_t)p_modem->index))
        {
          if (p_modem->file_received >= p_modem->file_length)
          {
            p_modem->type = YMODEM_TYPE_ERROR;
            ret = true;
            break;
          }

          p_modem->file_addr = p_modem->file_received;
          buf_length = p_modem->file_length - p_modem->file_addr;
          if (buf_length > p_modem->rx_packet.length)
          {
            buf_length = p_modem->rx_packet.length;
          }
          p_modem->file_buf_length = buf_length;
          p_modem->file_received += buf_length;
          p_modem->index = (uint8_t)(p_modem->index + 1U);

          p_modem->type = YMODEM_TYPE_DATA;
          ret = true;
        }
        else if (((p_modem->rx_packet.stx == YMODEM_SOH) ||
                  (p_modem->rx_packet.stx == YMODEM_STX)) &&
                 (p_modem->rx_packet.seq[0] ==
                     (uint8_t)((uint8_t)p_modem->index - 1U)))
        {
          ymodemPutch(p_modem, YMODEM_ACK);
        }
        else
        {
          ymodemPutch(p_modem, YMODEM_NACK);
        }
        break;

      case YMODEM_STATE_WAIT_LAST:
        if (p_modem->rx_packet.stx == YMODEM_EOT)
        {
          ymodemPutch(p_modem, YMODEM_ACK);
          ymodemPutch(p_modem, YMODEM_C);
          p_modem->state = YMODEM_STATE_WAIT_END;
        }
        else
        {
          p_modem->type = YMODEM_TYPE_ERROR;
          ret = true;
        }
        break;

      case YMODEM_STATE_WAIT_END:
        if (((p_modem->rx_packet.stx == YMODEM_SOH) ||
             (p_modem->rx_packet.stx == YMODEM_STX)) &&
            (p_modem->rx_packet.seq[0] == 0x00U) &&
            (p_modem->rx_packet.data[0] == 0x00U))
        {
          p_modem->type = YMODEM_TYPE_END;
        }
        else
        {
          p_modem->type = YMODEM_TYPE_ERROR;
        }
        ret = true;
        break;

      case YMODEM_STATE_WAIT_CANCEL:
        ymodemPutch(p_modem, YMODEM_ACK);
        p_modem->state = YMODEM_STATE_WAIT_HEAD;
        p_modem->type = YMODEM_TYPE_CANCEL;
        ret = true;
        break;
    }
  }
  else
  {
    if ((p_modem->state == YMODEM_STATE_WAIT_HEAD) &&
        (p_modem->rx_packet.state == YMODEM_PACKET_WAIT_FIRST))
    {
      if (millis()-p_modem->pre_time >= p_modem->start_time)
      {
        p_modem->pre_time = millis();
        ymodemPutch(p_modem, YMODEM_C);
      }
    }
  }

  return ret;
}

bool ymodemReceivePacket(ymodem_packet_t *p_packet, uint8_t data_in)
{
  bool ret = false;

  if ((p_packet == NULL) || (p_packet->data == NULL))
  {
    return false;
  }

  switch(p_packet->state)
  {
    case YMODEM_PACKET_WAIT_FIRST:
      if (data_in == YMODEM_SOH)
      {
        p_packet->length = 128;
        p_packet->stx = data_in;
        p_packet->state = YMODEM_PACKET_WAIT_SEQ1;
      }
      if (data_in == YMODEM_STX)
      {
        p_packet->length = 1024;
        p_packet->stx = data_in;
        p_packet->state = YMODEM_PACKET_WAIT_SEQ1;
      }
      if (data_in == YMODEM_EOT)
      {
        p_packet->stx = data_in;
        ret = true;
      }
      if (data_in == YMODEM_CAN)
      {
        p_packet->stx = data_in;
        ret = true;
      }
      break;

    case YMODEM_PACKET_WAIT_SEQ1:
      p_packet->seq[0] = data_in;
      p_packet->state = YMODEM_PACKET_WAIT_SEQ2;
      break;

    case YMODEM_PACKET_WAIT_SEQ2:
      p_packet->seq[1] = data_in;
      if (p_packet->seq[0] == (uint8_t)(~data_in))
      {
        p_packet->index = 0;
        p_packet->state = YMODEM_PACKET_WAIT_DATA;
      }
      else
      {
        p_packet->state = YMODEM_PACKET_WAIT_FIRST;
      }
      break;

    case YMODEM_PACKET_WAIT_DATA:
      if (p_packet->index >= p_packet->length)
      {
        p_packet->state = YMODEM_PACKET_WAIT_FIRST;
        break;
      }
      p_packet->data[p_packet->index] = data_in;
      p_packet->index++;
      if (p_packet->index >= p_packet->length)
      {
        p_packet->state = YMODEM_PACKET_WAIT_CRCH;
      }
      break;

    case YMODEM_PACKET_WAIT_CRCH:
      p_packet->crc_recv = (data_in<<8);
      p_packet->state = YMODEM_PACKET_WAIT_CRCL;
      break;

    case YMODEM_PACKET_WAIT_CRCL:
      p_packet->crc_recv |= (data_in<<0);
      p_packet->state = YMODEM_PACKET_WAIT_FIRST;

      p_packet->crc = crc16(p_packet->data, p_packet->length);

      if (p_packet->crc == p_packet->crc_recv)
      {
        ret = true;
      }
      //uartPrintf(_DEF_UART1, "crc %X %X\n", p_packet->crc, p_packet->crc_recv);
      break;
  }

  return ret;
}


#define CRC_POLY 0x1021

static uint16_t crc_update(uint16_t crc_in, int incr)
{
  uint16_t xor = crc_in >> 15;
  uint16_t out = crc_in << 1;

  if (incr)
  {
    out++;
  }

  if (xor)
  {
    out ^= CRC_POLY;
  }

  return out;
}

static uint16_t crc16(uint8_t *data, uint16_t size)
{
  uint16_t crc, i;

  for (crc = 0; size > 0; size--, data++)
  {
    for (i = 0x80; i; i >>= 1)
    {
      crc = crc_update(crc, *data & i);
    }
  }

  for (i = 0; i < 16; i++)
  {
    crc = crc_update(crc, 0);
  }

  return crc;
}


#ifdef _USE_HW_CLI
void cliYmodem(cli_args_t *args)
{
  bool ret = false;
  ymodem_t ymodem;
  bool keep_loop;
  bool transfer_ok;


  if ((args != NULL) &&
      (args->argc == 1) &&
      (args->isStr(0, "down") == true))
  {
    keep_loop = ymodemOpen(&ymodem, _DEF_UART1);
    transfer_ok = false;

    while (keep_loop)
    {
      if (ymodemReceive(&ymodem) == true)
      {
        bool packet_ok = false;

        switch(ymodem.type)
        {
          case YMODEM_TYPE_START:
            packet_ok = (ymodem.file_length > 0U);
            break;

          case YMODEM_TYPE_DATA:
            packet_ok = true;
            break;

          case YMODEM_TYPE_END:
            packet_ok = true;
            transfer_ok = true;
            keep_loop = false;
            break;

          case YMODEM_TYPE_CANCEL:
            keep_loop = false;
            break;

          case YMODEM_TYPE_ERROR:
          default:
            keep_loop = false;
            break;
        }

        if (packet_ok == true)
        {
          if (ymodemAck(&ymodem) != true)
          {
            transfer_ok = false;
            keep_loop = false;
          }
        }
        else if (ymodem.type != YMODEM_TYPE_CANCEL)
        {
          ymodemCancel(&ymodem);
        }
      }
    }

    cliPrintf("ymodem %s\n", transfer_ok ? "OK" : "Fail");
    ret = true;
  }


  if (ret != true)
  {
    cliPrintf("ymodem down\n");
  }
}
#endif
