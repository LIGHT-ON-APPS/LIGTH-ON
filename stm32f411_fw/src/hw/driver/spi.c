/*
 * spi.c
 *
 *  Created on: Dec 30, 2025
 *      Author: TEMP
 */

#include "spi.h"


#ifdef _USE_HW_SPI


typedef struct
{
  bool is_open;
  volatile bool is_tx_done;
  volatile bool is_rx_done;

  void (*func_tx)(void);
  void (*func_rx)(void);

  SPI_HandleTypeDef *h_spi;
  DMA_HandleTypeDef *h_dma_tx;
  DMA_HandleTypeDef *h_dma_rx;
} spi_t;



spi_t spi_tbl[SPI_MAX_CH];



SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_tx;
DMA_HandleTypeDef hdma_spi1_rx;


bool spiInit(void)
{
  bool ret = true;


  for (int i=0; i<SPI_MAX_CH; i++)
  {
    spi_tbl[i].is_open = false;
    spi_tbl[i].is_tx_done = true;
    spi_tbl[i].is_rx_done = true;
    spi_tbl[i].func_tx = NULL;
    spi_tbl[i].func_rx = NULL;
  }

  return ret;
}

bool spiBegin(uint8_t ch)
{
  bool ret = false;
  spi_t *p_spi;

  if (ch >= SPI_MAX_CH)
  {
    return false;
  }

  p_spi = &spi_tbl[ch];

  switch(ch)
  {
    case _DEF_SPI1:
      p_spi->h_spi = &hspi1;
      p_spi->h_dma_tx = &hdma_spi1_tx;
      p_spi->h_dma_rx = &hdma_spi1_rx;

      hspi1.Instance              = SPI1;
      hspi1.Init.Mode             = SPI_MODE_MASTER;
      hspi1.Init.Direction        = SPI_DIRECTION_2LINES;
      hspi1.Init.DataSize         = SPI_DATASIZE_8BIT;
      hspi1.Init.CLKPolarity      = SPI_POLARITY_LOW;
      hspi1.Init.CLKPhase         = SPI_PHASE_1EDGE;
      hspi1.Init.NSS              = SPI_NSS_SOFT;
      // APB2 96 MHz / 8 = 12 MHz (ILI9488 4-line SPI max. 20 MHz)
      /*
       * SYSCLK 96 MHz, APB2 분주 1 이므로 SPI1 은 96 MHz / prescaler 다.
       * 8 이면 12 MHz 인데, ILI9488 은 SPI 로 16bpp 를 받지 못해 픽셀당 3바이트를
       * 보내야 한다. 12 MHz 에서는 화면 일부만 갱신해도 전송에 100 ms 단위가 걸려
       * 계기판 갱신이 눈에 띄게 끊긴다. 4(24 MHz)로 올려 전송 시간을 절반으로
       * 줄인다.
       *
       * 데이터시트상 쓰기 사이클 하한은 50 ns(20 MHz)라 24 MHz 는 그보다 빠르다.
       * 배선이 길거나 화면에 노이즈/찌그러짐이 보이면 여기를 8 로 되돌리면 된다.
       */
      hspi1.Init.BaudRatePrescaler= SPI_BAUDRATEPRESCALER_4;
      hspi1.Init.FirstBit         = SPI_FIRSTBIT_MSB;
      hspi1.Init.TIMode           = SPI_TIMODE_DISABLE;
      hspi1.Init.CRCCalculation   = SPI_CRCCALCULATION_DISABLE;
      hspi1.Init.CRCPolynomial    = 10;

      if (HAL_SPI_Init(&hspi1) == HAL_OK)
      {
        p_spi->is_open = true;
        ret = true;
      }
      break;
  }

  return ret;
}

void spiSetDataMode(uint8_t ch, uint8_t dataMode)
{
  spi_t *p_spi;
  uint32_t clk_polarity;
  uint32_t clk_phase;

  if (ch >= SPI_MAX_CH)
  {
    return;
  }

  p_spi = &spi_tbl[ch];
  if ((p_spi->is_open == false) || (p_spi->h_spi == NULL))
  {
    return;
  }


  switch( dataMode )
  {
    // CPOL=0, CPHA=0
    case SPI_MODE0:
      clk_polarity = SPI_POLARITY_LOW;
      clk_phase = SPI_PHASE_1EDGE;
      break;

    // CPOL=0, CPHA=1
    case SPI_MODE1:
      clk_polarity = SPI_POLARITY_LOW;
      clk_phase = SPI_PHASE_2EDGE;
      break;

    // CPOL=1, CPHA=0
    case SPI_MODE2:
      clk_polarity = SPI_POLARITY_HIGH;
      clk_phase = SPI_PHASE_1EDGE;
      break;

    // CPOL=1, CPHA=1
    case SPI_MODE3:
      clk_polarity = SPI_POLARITY_HIGH;
      clk_phase = SPI_PHASE_2EDGE;
      break;

    default:
      return;
  }

  if ((p_spi->h_spi->Init.CLKPolarity == clk_polarity) &&
      (p_spi->h_spi->Init.CLKPhase == clk_phase))
  {
    return;
  }

  p_spi->h_spi->Init.CLKPolarity = clk_polarity;
  p_spi->h_spi->Init.CLKPhase = clk_phase;
  if (HAL_SPI_Init(p_spi->h_spi) != HAL_OK)
  {
    p_spi->is_open = false;
  }
}


void spiSetBitWidth(uint8_t ch, uint8_t bit_width)
{
  spi_t *p_spi;
  uint32_t data_size;

  if (ch >= SPI_MAX_CH)
  {
    return;
  }

  p_spi = &spi_tbl[ch];
  if ((p_spi->is_open == false) || (p_spi->h_spi == NULL))
  {
    return;
  }

  if (bit_width == 8U)
  {
    data_size = SPI_DATASIZE_8BIT;
  }
  else if (bit_width == 16U)
  {
    data_size = SPI_DATASIZE_16BIT;
  }
  else
  {
    return;
  }

  if (p_spi->h_spi->Init.DataSize == data_size)
  {
    return;
  }

  p_spi->h_spi->Init.DataSize = data_size;
  if (HAL_SPI_Init(p_spi->h_spi) != HAL_OK)
  {
    p_spi->is_open = false;
  }
}

// 1bye씩
uint8_t spiTransfer8(uint8_t ch, uint8_t data)
{
  uint8_t ret = 0;
  spi_t *p_spi;

  if (ch >= SPI_MAX_CH)
  {
    return 0;
  }

  p_spi = &spi_tbl[ch];
  if ((p_spi->is_open == false) || (p_spi->h_spi == NULL))
  {
    return 0;
  }

  if (HAL_SPI_TransmitReceive(p_spi->h_spi, &data, &ret, 1, 0xffff) != HAL_OK)
  {
    return 0;
  }

  return ret;
}

// 2bye씩
// 상위비트 먼저 처리하는 이유는
// 먼저 MSB부터 처리하는 방식이기 때문이다.
uint16_t spiTransfer16(uint8_t ch, uint16_t data)
{
  uint8_t tBuf[2];
  uint8_t rBuf[2] = {0, 0};
  uint16_t ret = 0;
  spi_t *p_spi;

  if (ch >= SPI_MAX_CH)
  {
    return 0;
  }

  p_spi = &spi_tbl[ch];
  if ((p_spi->is_open == false) || (p_spi->h_spi == NULL))
  {
    return 0;
  }

  if (p_spi->h_spi->Init.DataSize == SPI_DATASIZE_8BIT)
  {
    tBuf[1] = (uint8_t)data;
    tBuf[0] = (uint8_t)(data>>8);
    if (HAL_SPI_TransmitReceive(p_spi->h_spi, (uint8_t *)&tBuf, (uint8_t *)&rBuf, 2, 0xffff) != HAL_OK)
    {
      return 0;
    }

    ret = rBuf[0];
    ret <<= 8;
    ret += rBuf[1];
  }
  else
  {
    if (HAL_SPI_TransmitReceive(p_spi->h_spi, (uint8_t *)&data, (uint8_t *)&ret, 1, 0xffff) != HAL_OK)
    {
      return 0;
    }
  }

  return ret;
}

bool spiWrite(uint8_t ch, const uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  spi_t *p_spi;

  if ((ch >= SPI_MAX_CH) || (p_data == NULL) || (length == 0U))
  {
    return false;
  }

  p_spi = &spi_tbl[ch];

  if (p_spi->is_open == false)
  {
    return false;
  }

  while (length > 0U)
  {
    uint16_t tx_length = (length > UINT16_MAX) ? UINT16_MAX : (uint16_t)length;

    if (HAL_SPI_Transmit(p_spi->h_spi, (uint8_t *)p_data, tx_length, timeout) != HAL_OK)
    {
      return false;
    }

    p_data += tx_length;
    length -= tx_length;
  }

  return true;
}

// DMA 송신 시작하는 함수 프레임 버퍼 데이터를 시작해라 false에서 시작하고 나면 Tx call back이 호출되면서 true로 바뀌게 됨.
void spiDmaTxStart(uint8_t spi_ch, uint8_t *p_buf, uint32_t length)
{
  spi_t *p_spi;

  if ((spi_ch >= SPI_MAX_CH) || (p_buf == NULL) ||
      (length == 0U) || (length > UINT16_MAX))
  {
    return;
  }

  p_spi = &spi_tbl[spi_ch];
  if ((p_spi->is_open == false) || (p_spi->h_spi == NULL))
  {
    return;
  }

  p_spi->is_tx_done = false;
  if (HAL_SPI_Transmit_DMA(p_spi->h_spi, p_buf, (uint16_t)length) != HAL_OK)
  {
    p_spi->is_tx_done = true;
  }
}

// timeout만큼 DMA전송이 완료될때까지 기다리고 되면 무시하고 return 함
void spiDmaTxTransfer(uint8_t ch, void *buf, uint32_t length, uint32_t timeout)
{
  uint32_t t_time;


  spiDmaTxStart(ch, (uint8_t *)buf, length);

  t_time = millis();

  if (timeout == 0) return;

  while(1)
  {
    if(spiDmaTxIsDone(ch))
    {
      break;
    }
    if((millis()-t_time) > timeout)
    {
      if ((ch < SPI_MAX_CH) && (spi_tbl[ch].h_spi != NULL))
      {
        (void)HAL_SPI_Abort(spi_tbl[ch].h_spi);
        spi_tbl[ch].is_tx_done = true;
      }
      break;
    }
  }
}

void spiDmaRxStart(uint8_t spi_ch, uint8_t *p_buf, uint32_t length)
{
  spi_t *p_spi;

  if ((spi_ch >= SPI_MAX_CH) || (p_buf == NULL) ||
      (length == 0U) || (length > UINT16_MAX))
  {
    return;
  }

  p_spi = &spi_tbl[spi_ch];
  if ((p_spi->is_open == false) || (p_spi->h_spi == NULL))
  {
    return;
  }

  p_spi->is_rx_done = false;
  if (HAL_SPI_Receive_DMA(p_spi->h_spi, p_buf, (uint16_t)length) != HAL_OK)
  {
    p_spi->is_rx_done = true;
  }
}

void spiDmaRxTransfer(uint8_t ch, void *buf, uint32_t length, uint32_t timeout)
{
  uint32_t t_time;


  spiDmaRxStart(ch, (uint8_t *)buf, length);

  t_time = millis();

  if (timeout == 0) return;

  while(1)
  {
    if(spiDmaRxIsDone(ch))
    {
      break;
    }
    if((millis()-t_time) > timeout)
    {
      if ((ch < SPI_MAX_CH) && (spi_tbl[ch].h_spi != NULL))
      {
        (void)HAL_SPI_Abort(spi_tbl[ch].h_spi);
        spi_tbl[ch].is_rx_done = true;
      }
      break;
    }
  }
}

bool spiDmaTxIsDone(uint8_t ch)
{
  spi_t *p_spi;

  if (ch >= SPI_MAX_CH)
  {
    return true;
  }

  p_spi = &spi_tbl[ch];
  if (p_spi->is_open == false)
  {
    return true;
  }

  return p_spi->is_tx_done;
}

bool spiDmaRxIsDone(uint8_t ch)
{
  spi_t *p_spi;

  if (ch >= SPI_MAX_CH)
  {
    return true;
  }

  p_spi = &spi_tbl[ch];
  if (p_spi->is_open == false)
  {
    return true;
  }

  return p_spi->is_rx_done;
}

void spiAttachTxInterrupt(uint8_t ch, void (*func)())
{
  spi_t *p_spi;

  if (ch >= SPI_MAX_CH)
  {
    return;
  }

  p_spi = &spi_tbl[ch];
  if (p_spi->is_open == false)
  {
    return;
  }

  p_spi->func_tx = func;
}

void spiAttachRxInterrupt(uint8_t ch, void (*func)())
{
  spi_t *p_spi;

  if (ch >= SPI_MAX_CH)
  {
    return;
  }

  p_spi = &spi_tbl[ch];
  if (p_spi->is_open == false)
  {
    return;
  }

  p_spi->func_rx = func;
}



void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)		//SPI 다 출력하고 나서 완료되면 interrupt 콜백함 is_tx_done이 true이면 완료되었구나를 알게됨
{
  spi_t  *p_spi;

  if ((hspi != NULL) &&
      (spi_tbl[_DEF_SPI1].h_spi != NULL) &&
      (hspi->Instance == spi_tbl[_DEF_SPI1].h_spi->Instance))
  {
    p_spi = &spi_tbl[_DEF_SPI1];

    p_spi->is_tx_done = true;

    if (p_spi->func_tx != NULL)
    {
      (*p_spi->func_tx)();
    }
  }
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
  spi_t  *p_spi;

  if ((hspi != NULL) &&
      (spi_tbl[_DEF_SPI1].h_spi != NULL) &&
      (hspi->Instance == spi_tbl[_DEF_SPI1].h_spi->Instance))
  {
    p_spi = &spi_tbl[_DEF_SPI1];

    p_spi->is_rx_done = true;

    if (p_spi->func_rx != NULL)
    {
      (*p_spi->func_rx)();
    }
  }
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
  spi_t  *p_spi;

  if ((hspi != NULL) &&
      (spi_tbl[_DEF_SPI1].h_spi != NULL) &&
      (hspi->Instance == spi_tbl[_DEF_SPI1].h_spi->Instance))
  {
    p_spi = &spi_tbl[_DEF_SPI1];

    p_spi->is_tx_done = true;
    p_spi->is_rx_done = true;

    if (p_spi->func_rx != NULL)
    {
      (*p_spi->func_rx)();
    }
  }
}

void HAL_SPI_MspInit(SPI_HandleTypeDef* spiHandle)
{

	GPIO_InitTypeDef GPIO_InitStruct = {0};
	  if(spiHandle->Instance==SPI1)
	  {
	  /* USER CODE BEGIN SPI1_MspInit 0 */
	    __HAL_RCC_DMA2_CLK_ENABLE();

	  /* USER CODE END SPI1_MspInit 0 */
	    /* SPI1 clock enable */
	    __HAL_RCC_SPI1_CLK_ENABLE();

	    __HAL_RCC_GPIOA_CLK_ENABLE();
	    /**SPI1 GPIO Configuration
	    PA5     ------> SPI1_SCK
	    PA6     ------> SPI1_MISO
	    PA7     ------> SPI1_MOSI
	    */
	    GPIO_InitStruct.Pin = GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7;
	    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	    GPIO_InitStruct.Pull = GPIO_NOPULL;
	    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
	    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	    /* SPI1 DMA Init */
	    /* SPI1_RX Init */
	    hdma_spi1_rx.Instance = DMA2_Stream0;
	    hdma_spi1_rx.Init.Channel = DMA_CHANNEL_3;
	    hdma_spi1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
	    hdma_spi1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
	    hdma_spi1_rx.Init.MemInc = DMA_MINC_ENABLE;
	    hdma_spi1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
	    hdma_spi1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
	    hdma_spi1_rx.Init.Mode = DMA_NORMAL;
	    hdma_spi1_rx.Init.Priority = DMA_PRIORITY_LOW;
	    hdma_spi1_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
	    if (HAL_DMA_Init(&hdma_spi1_rx) != HAL_OK)
	    {
	      Error_Handler();
	    }

	    __HAL_LINKDMA(spiHandle,hdmarx,hdma_spi1_rx);

	    /* SPI1_TX Init */
	    hdma_spi1_tx.Instance = DMA2_Stream2;
	    hdma_spi1_tx.Init.Channel = DMA_CHANNEL_2;
	    hdma_spi1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
	    hdma_spi1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
	    hdma_spi1_tx.Init.MemInc = DMA_MINC_ENABLE;
	    hdma_spi1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
	    hdma_spi1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
	    hdma_spi1_tx.Init.Mode = DMA_NORMAL;
	    hdma_spi1_tx.Init.Priority = DMA_PRIORITY_LOW;
	    hdma_spi1_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
	    if (HAL_DMA_Init(&hdma_spi1_tx) != HAL_OK)
	    {
	      Error_Handler();
	    }

	    __HAL_LINKDMA(spiHandle,hdmatx,hdma_spi1_tx);

	  /* USER CODE BEGIN SPI1_MspInit 1 */
	    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 5, 0);
	    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
	    HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 5, 0);
	    HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);

	  /* USER CODE END SPI1_MspInit 1 */
	  }
}

void HAL_SPI_MspDeInit(SPI_HandleTypeDef* spiHandle)
{

	  if(spiHandle->Instance==SPI1)
	  {
	  /* USER CODE BEGIN SPI1_MspDeInit 0 */

	  /* USER CODE END SPI1_MspDeInit 0 */
	    /* Peripheral clock disable */
	    __HAL_RCC_SPI1_CLK_DISABLE();

	    /**SPI1 GPIO Configuration
	    PA5     ------> SPI1_SCK
	    PA6     ------> SPI1_MISO
	    PA7     ------> SPI1_MOSI
	    */
	    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7);

	    /* SPI1 DMA DeInit */
	    HAL_DMA_DeInit(spiHandle->hdmarx);
	    HAL_DMA_DeInit(spiHandle->hdmatx);
	  /* USER CODE BEGIN SPI1_MspDeInit 1 */

	  /* USER CODE END SPI1_MspDeInit 1 */
	  }
}

#endif
