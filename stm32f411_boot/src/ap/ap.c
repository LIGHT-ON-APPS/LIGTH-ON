/*
 * ap.c
 *
 *  Created on: Dec 6, 2020
 *      Author: baram
 */


#include "ap.h"


#define SRAM_ADDR_START          0x20000000U
#define SRAM_ADDR_END            0x20020000U


static bool bootIsAppValid(void);
static void bootJumpToApp(void);
static void bootStartApp(uint32_t stack_addr, uint32_t reset_addr)
    __attribute__((naked, noreturn));
static bool bootDownload(void);
static void cliBoot(cli_args_t *args);


void apInit(void)
{
  delay(20);

  if ((buttonGetPressed(_DEF_BUTTON1) != true) && (bootIsAppValid() == true))
  {
    bootJumpToApp();
  }

  cliOpen(_DEF_UART1, 57600);
  cliAdd("boot", cliBoot);
}

void apMain(void)
{
  uint32_t pre_time;

  pre_time = millis();
  while (1)
  {
    if (millis() - pre_time >= 500U)
    {
      pre_time = millis();
      ledToggle(_DEF_LED1);
    }

    cliMain();
  }
}

static bool bootIsAppValid(void)
{
  uint32_t stack_addr;
  uint32_t reset_addr;
  uint32_t reset_code_addr;

  stack_addr = *((volatile uint32_t *)FLASH_ADDR_FW);
  reset_addr = *((volatile uint32_t *)(FLASH_ADDR_FW + sizeof(uint32_t)));
  reset_code_addr = reset_addr & ~1U;

  if ((stack_addr < SRAM_ADDR_START) ||
      (stack_addr > SRAM_ADDR_END) ||
      ((stack_addr & 0x7U) != 0U))
  {
    return false;
  }

  if (((reset_addr & 0x1U) == 0U) ||
      (reset_code_addr < FLASH_ADDR_FW) ||
      (reset_code_addr >= FLASH_ADDR_END))
  {
    return false;
  }

  return true;
}

static void bootJumpToApp(void)
{
  uint32_t stack_addr;
  uint32_t reset_addr;

  if (bootIsAppValid() != true)
  {
    return;
  }

  stack_addr = *((volatile uint32_t *)FLASH_ADDR_FW);
  reset_addr = *((volatile uint32_t *)(FLASH_ADDR_FW + sizeof(uint32_t)));

  usbDeInit();
  HAL_RCC_DeInit();
  HAL_DeInit();

  __disable_irq();

  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL = 0U;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

  for (uint32_t i = 0U; i < 8U; i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFFU;
    NVIC->ICPR[i] = 0xFFFFFFFFU;
  }

  SCB->VTOR = FLASH_ADDR_FW;
  __DSB();
  __ISB();

  bootStartApp(stack_addr, reset_addr);
}

static void bootStartApp(uint32_t stack_addr, uint32_t reset_addr)
{
  __asm volatile(
      "msr msp, r0       \n"
      "movs r0, #0       \n"
      "msr control, r0   \n"
      "msr basepri, r0   \n"
      "msr faultmask, r0 \n"
      "msr primask, r0   \n"
      "isb               \n"
      "bx r1             \n");
}

static bool bootDownload(void)
{
  bool ret = false;
  bool keep_loop = true;
  bool transfer_started = false;
  uint8_t err_code = 0U;
  uint32_t expected_length = 0U;
  uint32_t bytes_written = 0U;
  uint32_t pre_time;
  ymodem_t ymodem;

  if (ymodemOpen(&ymodem, _DEF_UART1) != true)
  {
    cliPrintf("download open fail\n");
    return false;
  }

  cliPrintf("download ...\n");
  pre_time = millis();

  while (keep_loop)
  {
    if (ymodemReceive(&ymodem) == true)
    {
      bool packet_ok = false;

      pre_time = millis();

      switch (ymodem.type)
      {
        case YMODEM_TYPE_START:
          if ((ymodem.file_length == 0U) ||
              (ymodem.file_length > FLASH_SIZE_FW))
          {
            err_code = 6U;
            keep_loop = false;
            break;
          }

          if (flashErase(FLASH_ADDR_FW, ymodem.file_length) != true)
          {
            err_code = 1U;
            keep_loop = false;
            break;
          }

          expected_length = ymodem.file_length;
          bytes_written = 0U;
          transfer_started = true;
          packet_ok = true;
          break;

        case YMODEM_TYPE_DATA:
          if ((transfer_started != true) ||
              (ymodem.file_buf == NULL) ||
              (ymodem.file_buf_length == 0U) ||
              (ymodem.file_buf_length > ymodem.rx_packet.length) ||
              (ymodem.file_addr != bytes_written) ||
              (ymodem.file_addr > expected_length) ||
              (ymodem.file_buf_length > (expected_length - ymodem.file_addr)) ||
              (ymodem.file_received !=
                  (ymodem.file_addr + ymodem.file_buf_length)))
          {
            err_code = 6U;
            keep_loop = false;
            break;
          }

          if (flashWrite(FLASH_ADDR_FW + ymodem.file_addr,
                         ymodem.file_buf,
                         ymodem.file_buf_length) != true)
          {
            err_code = 2U;
            keep_loop = false;
            break;
          }

          bytes_written += ymodem.file_buf_length;
          packet_ok = true;
          break;

        case YMODEM_TYPE_END:
          if ((transfer_started == true) &&
              (bytes_written == expected_length) &&
              (ymodem.file_received == expected_length) &&
              (bootIsAppValid() == true))
          {
            ret = true;
            packet_ok = true;
          }
          else
          {
            err_code = 7U;
          }
          keep_loop = false;
          break;

        case YMODEM_TYPE_CANCEL:
          keep_loop = false;
          err_code = 3U;
          break;

        case YMODEM_TYPE_ERROR:
        default:
          keep_loop = false;
          err_code = 4U;
          break;
      }

      if (packet_ok == true)
      {
        if (ymodemAck(&ymodem) != true)
        {
          ret = false;
          keep_loop = false;
          err_code = 8U;
          ymodemCancel(&ymodem);
        }
      }
      else if (ymodem.type != YMODEM_TYPE_CANCEL)
      {
        ymodemCancel(&ymodem);
      }
    }

    if (millis() - pre_time >= 10U * 1000U)
    {
      keep_loop = false;
      err_code = 5U;
      ymodemCancel(&ymodem);
    }
  }

  if (ret == true)
  {
    cliPrintf("OK\n");
  }
  else
  {
    cliPrintf("Fail %d\n", err_code);
  }

  return ret;
}

static void cliBoot(cli_args_t *args)
{
  bool ret = false;

  if (args == NULL)
  {
    return;
  }

  if ((args->argc == 1) && (args->isStr(0, "info") == true))
  {
    cliPrintf("Flash Start : 0x%X\n", FLASH_ADDR_FW);
    cliPrintf("Flash End   : 0x%X\n", FLASH_ADDR_END);
    cliPrintf("Flash Size  : %d KB\n", FLASH_SIZE_FW / 1024U);
    cliPrintf("App Valid   : %d\n", bootIsAppValid());
    ret = true;
  }

  if ((args->argc == 1) && (args->isStr(0, "down_fw") == true))
  {
    bootDownload();
    ret = true;
  }

  if ((args->argc == 1) && (args->isStr(0, "run") == true))
  {
    if (bootIsAppValid() == true)
    {
      cliPrintf("Run app\n");
      delay(10);
      bootJumpToApp();
    }
    else
    {
      cliPrintf("Invalid app\n");
    }
    ret = true;
  }

  if (ret != true)
  {
    cliPrintf("boot info\n");
    cliPrintf("boot down_fw\n");
    cliPrintf("boot run\n");
  }
}
