/*
 * uart.c
 *
 *  Created on: 2020. 12. 8.
 *      Author: baram
 */


#include "uart.h"
#include "cdc.h"


#ifdef _USE_HW_UART

#define _USE_UART1
#define _USE_UART2

#define UART1_RX_BUFFER_LENGTH             512U

static bool is_open[UART_MAX_CH];

#ifdef _USE_UART1
static uint8_t rx_buf[UART1_RX_BUFFER_LENGTH];
static volatile bool rx_restart_requested = false;
static volatile uint32_t rx_dma_wrap_count = 0U;
static volatile uint32_t rx_overrun_count = 0U;
static uint32_t rx_read_count = 0U;


UART_HandleTypeDef huart1;
DMA_HandleTypeDef  hdma_usart1_rx;
#endif


#ifdef _USE_UART1
static uint32_t uartGetUsart1RxProducerCount(void)
{
  uint32_t primask;
  uint32_t wrap_count;
  uint32_t remaining;
  uint32_t tc_before;
  uint32_t tc_after;
  uint32_t tc_flag;


  primask = __get_PRIMASK();
  __disable_irq();

  wrap_count = rx_dma_wrap_count;
  tc_flag = __HAL_DMA_GET_TC_FLAG_INDEX(&hdma_usart1_rx);

  do
  {
    tc_before = __HAL_DMA_GET_FLAG(&hdma_usart1_rx, tc_flag);
    remaining = __HAL_DMA_GET_COUNTER(&hdma_usart1_rx);
    tc_after = __HAL_DMA_GET_FLAG(&hdma_usart1_rx, tc_flag);
  } while (tc_before != tc_after);

  if (tc_after != 0U)
  {
    wrap_count++;
  }

  __set_PRIMASK(primask);

  return (wrap_count * UART1_RX_BUFFER_LENGTH) +
         ((UART1_RX_BUFFER_LENGTH - remaining) %
          UART1_RX_BUFFER_LENGTH);
}

static bool uartStartUsart1RxDma(void)
{
  rx_dma_wrap_count = 0U;
  rx_read_count = 0U;

  if (HAL_UART_Receive_DMA(&huart1, &rx_buf[0], sizeof(rx_buf)) != HAL_OK)
  {
    rx_restart_requested = true;
    return false;
  }

  /* Full-transfer IRQ counts DMA wraps; half-transfer callbacks are unused. */
  __HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);

  rx_restart_requested = false;

  return true;
}

static void uartRecoverUsart1RxDma(void)
{
  if (rx_restart_requested != true)
  {
    return;
  }

  rx_restart_requested = false;
  (void)HAL_UART_AbortReceive(&huart1);
  if (uartStartUsart1RxDma() != true)
  {
    rx_restart_requested = true;
  }
}

static uint32_t uartGetUsart1RxAvailable(void)
{
  uint32_t producer_count;
  uint32_t available;


  producer_count = uartGetUsart1RxProducerCount();
  available = producer_count - rx_read_count;

  if (available >= UART1_RX_BUFFER_LENGTH)
  {
    rx_read_count = producer_count;
    rx_overrun_count++;
    return 0U;
  }

  return available;
}
#endif


bool uartInit(void)
{
  for (int i=0; i<UART_MAX_CH; i++)
  {
    is_open[i] = false;
  }


  return true;
}

bool uartOpen(uint8_t ch, uint32_t baud)
{
  bool ret = false;


  switch(ch)
  {
    case _DEF_UART1:
      #ifdef _USE_UART1
      huart1.Instance         = USART1;
      huart1.Init.BaudRate    = baud;
      huart1.Init.WordLength  = UART_WORDLENGTH_8B;
      huart1.Init.StopBits    = UART_STOPBITS_1;
      huart1.Init.Parity      = UART_PARITY_NONE;
      huart1.Init.Mode        = UART_MODE_RX;
      huart1.Init.HwFlowCtl   = UART_HWCONTROL_NONE;
      huart1.Init.OverSampling= UART_OVERSAMPLING_16;

      if (is_open[ch] == true)
      {
        (void)HAL_UART_AbortReceive(&huart1);
        (void)HAL_UART_DeInit(&huart1);
      }
      is_open[ch] = false;
      rx_restart_requested = false;
      rx_dma_wrap_count = 0U;
      rx_read_count = 0U;
      rx_overrun_count = 0U;

      if (HAL_UART_Init(&huart1) != HAL_OK)
      {
        ret = false;
      }
      else
      {
        ret = true;
        is_open[ch] = true;

        if (uartStartUsart1RxDma() != true)
        {
          ret = false;
          is_open[ch] = false;
        }
      }
      #endif
      break;

    case _DEF_UART2:
      is_open[ch] = true;
      ret = true;
      break;
  }

  return ret;
}

bool uartFlush(uint8_t ch)
{
  switch(ch)
  {
    case _DEF_UART1:
      #ifdef _USE_UART1
      if (is_open[ch] != true)
      {
        return false;
      }

      uartRecoverUsart1RxDma();
      if (rx_restart_requested == true)
      {
        return false;
      }

      rx_read_count = uartGetUsart1RxProducerCount();
      return true;
      #else
      return false;
      #endif

    case _DEF_UART2:
      while (cdcAvailable() > 0U)
      {
        (void)cdcRead();
      }
      return true;
  }

  return false;
}

uint32_t uartAvailable(uint8_t ch)
{
  uint32_t ret = 0;

  switch(ch)
  {
    case _DEF_UART1:
      #ifdef _USE_UART1
      if (is_open[ch] == true)
      {
        uartRecoverUsart1RxDma();
        if (rx_restart_requested != true)
        {
          ret = uartGetUsart1RxAvailable();
        }
      }
      #endif
      break;

    case _DEF_UART2:
      ret = cdcAvailable();
      break;
  }

  return ret;
}

uint8_t uartRead(uint8_t ch)
{
  uint8_t ret = 0;

  switch(ch)
  {
    case _DEF_UART1:
      #ifdef _USE_UART1
      if ((is_open[ch] == true) &&
          (uartGetUsart1RxAvailable() > 0U))
      {
        ret = rx_buf[rx_read_count % UART1_RX_BUFFER_LENGTH];
        rx_read_count++;
      }
      #endif
      break;

    case _DEF_UART2:
      ret = cdcRead();
      break;
  }

  return ret;
}

uint32_t uartWrite(uint8_t ch, uint8_t *p_data, uint32_t length)
{
  uint32_t ret = 0;
  switch(ch)
  {
    case _DEF_UART1:
      /* The display link is receive-only on the F411. */
      ret = 0U;
      break;

    case _DEF_UART2:
      ret = cdcWrite(p_data, length);
      break;
  }

  return ret;
}

uint32_t uartPrintf(uint8_t ch, char *fmt, ...)
{
  char buf[256];
  va_list args;
  int len;
  uint32_t ret;

  if (fmt == NULL)
  {
    return 0;
  }

  va_start(args, fmt);
  len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (len < 0)
  {
    return 0;
  }

  if ((uint32_t)len >= sizeof(buf))
  {
    len = sizeof(buf) - 1U;
  }

  ret = uartWrite(ch, (uint8_t *)buf, (uint32_t)len);

  return ret;
}

uint32_t uartGetBaud(uint8_t ch)
{
  uint32_t ret = 0;


  switch(ch)
  {
    case _DEF_UART1:
      #ifdef _USE_UART1
      ret = huart1.Init.BaudRate;
      #endif
      break;

    case _DEF_UART2:
      ret = cdcGetBaud();
      break;
  }

  return ret;
}

uint32_t uartGetRxOverrunCount(uint8_t ch)
{
  if (ch == _DEF_UART1)
  {
#ifdef _USE_UART1
    return rx_overrun_count;
#endif
  }

  return 0U;
}



#ifdef _USE_UART1
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    rx_restart_requested = true;
    rx_overrun_count++;
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    rx_dma_wrap_count++;
  }
}




void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspInit 0 */

  /* USER CODE END USART1_MspInit 0 */
    /* USART1 clock enable */
    __HAL_RCC_USART1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    /**USART1 GPIO Configuration
    PA10     ------> USART1_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* USART1 DMA Init */
    /* USART1_RX Init */
    hdma_usart1_rx.Instance = DMA2_Stream5;
    hdma_usart1_rx.Init.Channel = DMA_CHANNEL_4;
    hdma_usart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart1_rx.Init.Mode = DMA_CIRCULAR;
    hdma_usart1_rx.Init.Priority = DMA_PRIORITY_LOW;
    hdma_usart1_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart1_rx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmarx,hdma_usart1_rx);

    HAL_NVIC_SetPriority(DMA2_Stream5_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream5_IRQn);

    /* USART1 interrupt Init */
    HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspInit 1 */

  /* USER CODE END USART1_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspDeInit 0 */

  /* USER CODE END USART1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART1_CLK_DISABLE();

    /**USART1 GPIO Configuration
    PA10     ------> USART1_RX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_10);

    /* USART1 DMA DeInit */
    if (uartHandle->hdmarx != NULL)
    {
      HAL_DMA_DeInit(uartHandle->hdmarx);
    }

    HAL_NVIC_DisableIRQ(DMA2_Stream5_IRQn);
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream5_IRQn);

    /* USART1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    HAL_NVIC_ClearPendingIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspDeInit 1 */

  /* USER CODE END USART1_MspDeInit 1 */
  }
}
#endif

#endif
