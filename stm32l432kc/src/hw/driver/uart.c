/*
 * uart.c
 *
 *  Created on: May 6, 2026
 *      Author: TEMP
 */


#include "uart.h"
#include "qbuffer.h"


#ifdef _USE_HW_UART

#define UART_CH_CLI                      _DEF_UART1
#define UART_CH_DISPLAY                  _DEF_UART2

#define UART_RX_BUFFER_LENGTH            256U
#define UART_PRINTF_BUFFER_LENGTH        256U
#define UART_DISPLAY_TX_BUFFER_LENGTH    256U
#define UART_DISPLAY_TX_BUFFER_COUNT     2U
#define UART_CLI_TX_TIMEOUT_MS           100U
#define UART_CLI_IRQ_PRIORITY            0U
#define UART_DISPLAY_IRQ_PRIORITY        6U

/*
 * Logical channel map
 *   UART_CH_CLI     (_DEF_UART1) -> USART2, PA2/PA15, blocking TX + IT RX
 *   UART_CH_DISPLAY (_DEF_UART2) -> USART1, PA9/PA10, DMA TX + IT RX
 */
typedef struct
{
  uint8_t buffer[UART_DISPLAY_TX_BUFFER_COUNT][UART_DISPLAY_TX_BUFFER_LENGTH];
  volatile bool busy;
  volatile bool pending;
  volatile uint8_t active_index;
  volatile uint16_t pending_length;
} uart_display_tx_t;


static volatile bool is_open[UART_MAX_CH];
static volatile bool is_rx_open[UART_MAX_CH];

static qbuffer_t qbuffer[UART_MAX_CH];
static uint8_t rx_buffer[UART_MAX_CH][UART_RX_BUFFER_LENGTH];
static uint8_t rx_data[UART_MAX_CH];

/* Physical USART1: display link. */
UART_HandleTypeDef huart1;
DMA_HandleTypeDef hdma_usart1_rx;
DMA_HandleTypeDef hdma_usart1_tx;

/* Physical USART2: CLI link. */
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart2_rx;

static uart_display_tx_t display_tx;


static uint32_t uartCriticalEnter(void)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  return primask;
}

static void uartCriticalExit(uint32_t primask)
{
  __set_PRIMASK(primask);
}

static UART_HandleTypeDef *uartGetHandle(uint8_t ch)
{
  switch (ch)
  {
    case UART_CH_CLI:
      return &huart2;

    case UART_CH_DISPLAY:
      return &huart1;

    default:
      return NULL;
  }
}

static USART_TypeDef *uartGetInstance(uint8_t ch)
{
  switch (ch)
  {
    case UART_CH_CLI:
      return USART2;

    case UART_CH_DISPLAY:
      return USART1;

    default:
      return NULL;
  }
}

static bool uartGetChannel(const UART_HandleTypeDef *huart, uint8_t *p_ch)
{
  if ((huart == NULL) || (p_ch == NULL))
  {
    return false;
  }

  if (huart->Instance == USART2)
  {
    *p_ch = UART_CH_CLI;
    return true;
  }

  if (huart->Instance == USART1)
  {
    *p_ch = UART_CH_DISPLAY;
    return true;
  }

  return false;
}

static void uartConfigure(UART_HandleTypeDef *huart,
                          USART_TypeDef *instance,
                          uint32_t baud)
{
  huart->Instance = instance;
  huart->Init.BaudRate = baud;
  huart->Init.WordLength = UART_WORDLENGTH_8B;
  huart->Init.StopBits = UART_STOPBITS_1;
  huart->Init.Parity = UART_PARITY_NONE;
  huart->Init.Mode = UART_MODE_TX_RX;
  huart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart->Init.OverSampling = UART_OVERSAMPLING_16;
  huart->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
}

static bool uartEnsureRxArmed(uint8_t ch, UART_HandleTypeDef *huart)
{
  if ((ch >= UART_MAX_CH) ||
      (huart == NULL) ||
      (is_open[ch] != true))
  {
    return false;
  }

  if (is_rx_open[ch] == true)
  {
    return true;
  }

  if (huart->RxState != HAL_UART_STATE_READY)
  {
    return false;
  }

  if (HAL_UART_Receive_IT(huart, &rx_data[ch], 1U) != HAL_OK)
  {
    return false;
  }

  is_rx_open[ch] = true;
  return true;
}

static void uartDisplayTxReset(void)
{
  uint32_t primask = uartCriticalEnter();

  display_tx.busy = false;
  display_tx.pending = false;
  display_tx.active_index = 0U;
  display_tx.pending_length = 0U;

  uartCriticalExit(primask);
}

static uint32_t uartDisplayWriteDma(uint8_t *p_data, uint32_t length)
{
  uint32_t primask;
  uint8_t buffer_index;

  if ((is_open[UART_CH_DISPLAY] != true) ||
      (p_data == NULL) ||
      (length == 0U) ||
      (length > UART_DISPLAY_TX_BUFFER_LENGTH))
  {
    return 0U;
  }

  primask = uartCriticalEnter();

  if (display_tx.busy == true)
  {
    /* Display packets are full snapshots, so only the newest pending one matters. */
    buffer_index = display_tx.active_index ^ 1U;
    memcpy(display_tx.buffer[buffer_index], p_data, length);
    display_tx.pending_length = (uint16_t)length;
    display_tx.pending = true;

    uartCriticalExit(primask);
    return length;
  }

  /* USART1 is owned by this driver. Do not overwrite a buffer if HAL disagrees. */
  if (huart1.gState != HAL_UART_STATE_READY)
  {
    uartCriticalExit(primask);
    return 0U;
  }

  buffer_index = display_tx.active_index;
  memcpy(display_tx.buffer[buffer_index], p_data, length);
  display_tx.busy = true;
  display_tx.pending = false;
  display_tx.pending_length = 0U;

  uartCriticalExit(primask);

  if (HAL_UART_Transmit_DMA(&huart1,
                            display_tx.buffer[buffer_index],
                            (uint16_t)length) != HAL_OK)
  {
    uartDisplayTxReset();
    return 0U;
  }

  return length;
}


bool uartInit(void)
{
  for (uint8_t ch = 0U; ch < UART_MAX_CH; ch++)
  {
    is_open[ch] = false;
    is_rx_open[ch] = false;
  }

  uartDisplayTxReset();

  return true;
}

bool uartOpen(uint8_t ch, uint32_t baud)
{
  UART_HandleTypeDef *huart;
  USART_TypeDef *instance;

  if (ch >= UART_MAX_CH)
  {
    return false;
  }

  huart = uartGetHandle(ch);
  instance = uartGetInstance(ch);
  if ((huart == NULL) || (instance == NULL))
  {
    return false;
  }

  /* DeInit uses Instance to select the matching MSP cleanup path. */
  huart->Instance = instance;
  if (is_open[ch] == true)
  {
    (void)HAL_UART_DeInit(huart);
  }

  is_open[ch] = false;
  is_rx_open[ch] = false;
  if (ch == UART_CH_DISPLAY)
  {
    uartDisplayTxReset();
  }

  uartConfigure(huart, instance, baud);
  qbufferCreate(&qbuffer[ch], &rx_buffer[ch][0], UART_RX_BUFFER_LENGTH);

  if (HAL_UART_Init(huart) != HAL_OK)
  {
    return false;
  }

  is_open[ch] = true;
  qbuffer[ch].in = 0U;
  qbuffer[ch].out = 0U;

  if (uartEnsureRxArmed(ch, huart) != true)
  {
    is_open[ch] = false;
    return false;
  }

  return true;
}

uint32_t uartAvailable(uint8_t ch)
{
  UART_HandleTypeDef *huart;

  if (ch >= UART_MAX_CH)
  {
    return 0U;
  }

  huart = uartGetHandle(ch);
  if ((huart == NULL) || (is_open[ch] != true))
  {
    return 0U;
  }

  /* Recover automatically if an RX error or re-arm failure stopped reception. */
  (void)uartEnsureRxArmed(ch, huart);
  return qbufferAvailable(&qbuffer[ch]);
}

uint8_t uartRead(uint8_t ch)
{
  uint8_t data = 0U;

  if ((ch >= UART_MAX_CH) || (is_open[ch] != true))
  {
    return 0U;
  }

  (void)qbufferRead(&qbuffer[ch], &data, 1U);
  return data;
}

uint32_t uartWrite(uint8_t ch, uint8_t *p_data, uint32_t length)
{
  if ((ch >= UART_MAX_CH) ||
      (is_open[ch] != true) ||
      (p_data == NULL) ||
      (length == 0U))
  {
    return 0U;
  }

  if (ch == UART_CH_CLI)
  {
    if (length > 0xFFFFU)
    {
      return 0U;
    }

    if (HAL_UART_Transmit(&huart2,
                          p_data,
                          (uint16_t)length,
                          UART_CLI_TX_TIMEOUT_MS) == HAL_OK)
    {
      return length;
    }

    return 0U;
  }

  if (ch == UART_CH_DISPLAY)
  {
    return uartDisplayWriteDma(p_data, length);
  }

  return 0U;
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  uint32_t primask;
  uint16_t length = 0U;
  uint8_t buffer_index = 0U;

  if ((huart == NULL) || (huart->Instance != USART1))
  {
    return;
  }

  primask = uartCriticalEnter();

  if (display_tx.pending == true)
  {
    display_tx.active_index ^= 1U;
    buffer_index = display_tx.active_index;
    length = display_tx.pending_length;
    display_tx.pending = false;
    display_tx.pending_length = 0U;
  }
  else
  {
    display_tx.busy = false;
  }

  uartCriticalExit(primask);

  if (length > 0U)
  {
    if (HAL_UART_Transmit_DMA(huart,
                              display_tx.buffer[buffer_index],
                              length) != HAL_OK)
    {
      uartDisplayTxReset();
    }
  }
}

uint32_t uartPrintf(uint8_t ch, char *fmt, ...)
{
  char buffer[UART_PRINTF_BUFFER_LENGTH];
  va_list args;
  int len;
  uint32_t ret = 0U;

  if (fmt == NULL)
  {
    return 0U;
  }

  va_start(args, fmt);
  len = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  if (len > 0)
  {
    if (len >= (int)sizeof(buffer))
    {
      len = (int)sizeof(buffer) - 1;
    }
    ret = uartWrite(ch, (uint8_t *)buffer, (uint32_t)len);
  }

  return ret;
}

uint32_t uartGetBaud(uint8_t ch)
{
  UART_HandleTypeDef *huart;

  if (ch >= UART_MAX_CH)
  {
    return 0U;
  }

  huart = uartGetHandle(ch);
  if (huart == NULL)
  {
    return 0U;
  }

  return huart->Init.BaudRate;
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  uint8_t ch;

  if (uartGetChannel(huart, &ch) != true)
  {
    return;
  }

  if ((ch == UART_CH_DISPLAY) &&
      ((huart->ErrorCode & HAL_UART_ERROR_DMA) != 0U))
  {
    uartDisplayTxReset();
  }

  is_rx_open[ch] = false;

  __HAL_UART_CLEAR_FLAG(huart,
                        UART_CLEAR_OREF |
                        UART_CLEAR_NEF  |
                        UART_CLEAR_FEF  |
                        UART_CLEAR_PEF);

  (void)uartEnsureRxArmed(ch, huart);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  uint8_t ch;

  if (uartGetChannel(huart, &ch) != true)
  {
    return;
  }

  is_rx_open[ch] = false;
  (void)qbufferWrite(&qbuffer[ch], &rx_data[ch], 1U);
  (void)uartEnsureRxArmed(ch, huart);
}

static void uartMspInitUsart1(UART_HandleTypeDef *uart_handle)
{
  GPIO_InitTypeDef gpio_init = {0};
  RCC_PeriphCLKInitTypeDef clock_init = {0};

  clock_init.PeriphClockSelection = RCC_PERIPHCLK_USART1;
  clock_init.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
  if (HAL_RCCEx_PeriphCLKConfig(&clock_init) != HAL_OK)
  {
    Error_Handler();
  }

  __HAL_RCC_USART1_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* USART1 display link: PA9 TX, PA10 RX. */
  gpio_init.Pin = GPIO_PIN_9 | GPIO_PIN_10;
  gpio_init.Mode = GPIO_MODE_AF_PP;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio_init.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(GPIOA, &gpio_init);

  /*
   * RX DMA is reserved by CubeMX but not started by this driver.
   * The active RX path is one-byte HAL_UART_Receive_IT().
   */
  hdma_usart1_rx.Instance = DMA1_Channel5;
  hdma_usart1_rx.Init.Request = DMA_REQUEST_2;
  hdma_usart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
  hdma_usart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_usart1_rx.Init.MemInc = DMA_MINC_ENABLE;
  hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_usart1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  hdma_usart1_rx.Init.Mode = DMA_NORMAL;
  hdma_usart1_rx.Init.Priority = DMA_PRIORITY_LOW;
  if (HAL_DMA_Init(&hdma_usart1_rx) != HAL_OK)
  {
    Error_Handler();
  }
  __HAL_LINKDMA(uart_handle, hdmarx, hdma_usart1_rx);

  /* Active display TX path: USART1_TX -> DMA1 channel 4, request 2. */
  hdma_usart1_tx.Instance = DMA1_Channel4;
  hdma_usart1_tx.Init.Request = DMA_REQUEST_2;
  hdma_usart1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdma_usart1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_usart1_tx.Init.MemInc = DMA_MINC_ENABLE;
  hdma_usart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_usart1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  hdma_usart1_tx.Init.Mode = DMA_NORMAL;
  hdma_usart1_tx.Init.Priority = DMA_PRIORITY_LOW;
  if (HAL_DMA_Init(&hdma_usart1_tx) != HAL_OK)
  {
    Error_Handler();
  }
  __HAL_LINKDMA(uart_handle, hdmatx, hdma_usart1_tx);

  HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, UART_DISPLAY_IRQ_PRIORITY, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);

  /* USART IRQ is still required for RX IT and the final UART TX-complete stage. */
  HAL_NVIC_SetPriority(USART1_IRQn, UART_DISPLAY_IRQ_PRIORITY, 0U);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
}

static void uartMspInitUsart2(UART_HandleTypeDef *uart_handle)
{
  GPIO_InitTypeDef gpio_init = {0};
  RCC_PeriphCLKInitTypeDef clock_init = {0};

  clock_init.PeriphClockSelection = RCC_PERIPHCLK_USART2;
  clock_init.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
  if (HAL_RCCEx_PeriphCLKConfig(&clock_init) != HAL_OK)
  {
    Error_Handler();
  }

  __HAL_RCC_USART2_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* USART2 CLI TX: PA2, AF7. */
  gpio_init.Pin = GPIO_PIN_2;
  gpio_init.Mode = GPIO_MODE_AF_PP;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio_init.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOA, &gpio_init);

  /* USART2 CLI RX: PA15, AF3. */
  gpio_init.Pin = GPIO_PIN_15;
  gpio_init.Alternate = GPIO_AF3_USART2;
  HAL_GPIO_Init(GPIOA, &gpio_init);

  /* Reserved CubeMX RX DMA; the active CLI RX path is HAL_UART_Receive_IT(). */
  hdma_usart2_rx.Instance = DMA1_Channel6;
  hdma_usart2_rx.Init.Request = DMA_REQUEST_2;
  hdma_usart2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
  hdma_usart2_rx.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_usart2_rx.Init.MemInc = DMA_MINC_ENABLE;
  hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_usart2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  hdma_usart2_rx.Init.Mode = DMA_CIRCULAR;
  hdma_usart2_rx.Init.Priority = DMA_PRIORITY_LOW;
  if (HAL_DMA_Init(&hdma_usart2_rx) != HAL_OK)
  {
    Error_Handler();
  }
  __HAL_LINKDMA(uart_handle, hdmarx, hdma_usart2_rx);

  HAL_NVIC_SetPriority(USART2_IRQn, UART_CLI_IRQ_PRIORITY, 0U);
  HAL_NVIC_EnableIRQ(USART2_IRQn);
}

void HAL_UART_MspInit(UART_HandleTypeDef *uart_handle)
{
  if (uart_handle == NULL)
  {
    return;
  }

  if (uart_handle->Instance == USART1)
  {
    uartMspInitUsart1(uart_handle);
  }
  else if (uart_handle->Instance == USART2)
  {
    uartMspInitUsart2(uart_handle);
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *uart_handle)
{
  if (uart_handle == NULL)
  {
    return;
  }

  if (uart_handle->Instance == USART1)
  {
    HAL_NVIC_DisableIRQ(DMA1_Channel4_IRQn);
    HAL_NVIC_ClearPendingIRQ(DMA1_Channel4_IRQn);
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    HAL_NVIC_ClearPendingIRQ(USART1_IRQn);

    if (uart_handle->hdmarx != NULL)
    {
      (void)HAL_DMA_DeInit(uart_handle->hdmarx);
    }
    if (uart_handle->hdmatx != NULL)
    {
      (void)HAL_DMA_DeInit(uart_handle->hdmatx);
    }

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9 | GPIO_PIN_10);
    __HAL_RCC_USART1_CLK_DISABLE();
  }
  else if (uart_handle->Instance == USART2)
  {
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    HAL_NVIC_ClearPendingIRQ(USART2_IRQn);

    if (uart_handle->hdmarx != NULL)
    {
      (void)HAL_DMA_DeInit(uart_handle->hdmarx);
    }

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2 | GPIO_PIN_15);
    __HAL_RCC_USART2_CLK_DISABLE();
  }
}

#endif
