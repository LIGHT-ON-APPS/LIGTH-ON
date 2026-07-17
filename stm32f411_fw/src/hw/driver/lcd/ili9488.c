/*
 * ili9488.c
 *
 *  Created on: Jun 25, 2026
 *      Author: young
 */

#include "spi.h"
#include "gpio.h"
#include "lcd/ili9488.h"
#include "lcd/ili9488_reg.h"

#ifdef _USE_HW_ILI9488
#ifdef _USE_HW_SPI
#ifdef _USE_HW_GPIO

#define ILI9488_SPI_CH                       _DEF_SPI1   // SPI1 (PA7 MOSI, PA5 SCK)
#define ILI9488_CS_CH    					 _DEF_GPIO2  // PB0
#define ILI9488_RST_CH   					 _DEF_GPIO3  // PA3
#define ILI9488_DC_CH    					 _DEF_GPIO4  // PA2

#define ILI9488_TX_CHUNK_PIXELS              128U
#define ILI9488_TX_CHUNK_BYTES               (ILI9488_TX_CHUNK_PIXELS * 3U)
#define ILI9488_SPI_TIMEOUT_MS                1000U

#define MADCTL_MY                            ILI9488_MADCTL_MY
#define MADCTL_MX                            ILI9488_MADCTL_MX
#define MADCTL_MV                            ILI9488_MADCTL_MV
#define MADCTL_ML                            ILI9488_MADCTL_ML
#define MADCTL_RGB                           0x00U
#define MADCTL_BGR                           ILI9488_MADCTL_BGR
#define MADCTL_MH                            ILI9488_MADCTL_MH


static uint8_t spi_ch = ILI9488_SPI_CH;

static void (*frameCallBack)(void) = NULL;
volatile static bool is_write_frame = false;


static bool writecommand(uint8_t c);
static bool writedata(uint8_t d);
static bool writedata_buf(const uint8_t *p_data, uint32_t length);
static void ili9488GpioInit(void);
static bool ili9488InitRegs(void);


static void TransferDoneISR(void)
{
  if (is_write_frame == true)
  {
    is_write_frame = false;
    gpioPinWrite(ILI9488_CS_CH, _DEF_HIGH);

    if (frameCallBack != NULL)
    {
      frameCallBack();
    }
  }
}

bool ili9488Init(void)
{
  return ili9488Reset();
}

bool ili9488InitDriver(lcd_driver_t *p_driver)
{
  if (p_driver == NULL)
  {
    return false;
  }

  p_driver->init        = ili9488Init;
  p_driver->reset       = ili9488Reset;
  p_driver->setWindow   = ili9488SetWindow;
  p_driver->getWidth    = ili9488GetWidth;
  p_driver->getHeight   = ili9488GetHeight;
  p_driver->setCallBack = ili9488SetCallBack;
  p_driver->sendBuffer  = ili9488SendBuffer;

  return true;
}

bool ili9488Reset(void)
{
  ili9488GpioInit();

  is_write_frame = false;

  gpioPinWrite(ILI9488_DC_CH,  _DEF_HIGH);
  gpioPinWrite(ILI9488_CS_CH,  _DEF_HIGH);
  gpioPinWrite(ILI9488_RST_CH, _DEF_HIGH);

  if (spiBegin(spi_ch) != true)
  {
    return false;
  }

  spiSetBitWidth(spi_ch, 8);
  spiSetDataMode(spi_ch, SPI_MODE0);
  spiAttachTxInterrupt(spi_ch, TransferDoneISR);

  delay(5);

  gpioPinWrite(ILI9488_RST_CH, _DEF_LOW);
  delay(20);
  gpioPinWrite(ILI9488_RST_CH, _DEF_HIGH);
  delay(120);

  if (ili9488InitRegs() != true)
  {
    return false;
  }

  if (ili9488SetRotation(0) != true)
  {
    return false;
  }

  return true;
}

uint16_t ili9488GetWidth(void)
{
  return HW_LCD_WIDTH;
}

uint16_t ili9488GetHeight(void)
{
  return HW_LCD_HEIGHT;
}

static bool writecommand(uint8_t c)
{
  bool ret;

  spiSetBitWidth(spi_ch, 8);

  gpioPinWrite(ILI9488_DC_CH, _DEF_LOW);
  gpioPinWrite(ILI9488_CS_CH, _DEF_LOW);

  ret = spiWrite(spi_ch, &c, 1, ILI9488_SPI_TIMEOUT_MS);

  gpioPinWrite(ILI9488_CS_CH, _DEF_HIGH);

  return ret;
}

static bool writedata(uint8_t d)
{
  bool ret;

  spiSetBitWidth(spi_ch, 8);

  gpioPinWrite(ILI9488_DC_CH, _DEF_HIGH);
  gpioPinWrite(ILI9488_CS_CH, _DEF_LOW);

  ret = spiWrite(spi_ch, &d, 1, ILI9488_SPI_TIMEOUT_MS);

  gpioPinWrite(ILI9488_CS_CH, _DEF_HIGH);

  return ret;
}

static bool writedata_buf(const uint8_t *p_data, uint32_t length)
{
  bool ret;

  if ((p_data == NULL) || (length == 0U))
  {
    return false;
  }

  spiSetBitWidth(spi_ch, 8);

  gpioPinWrite(ILI9488_DC_CH, _DEF_HIGH);
  gpioPinWrite(ILI9488_CS_CH, _DEF_LOW);

  ret = spiWrite(spi_ch, p_data, length, ILI9488_SPI_TIMEOUT_MS);

  gpioPinWrite(ILI9488_CS_CH, _DEF_HIGH);

  return ret;
}

static void ili9488GpioInit(void)
{
  gpioPinMode(ILI9488_CS_CH, _DEF_OUTPUT);
  gpioPinMode(ILI9488_DC_CH, _DEF_OUTPUT);
  gpioPinMode(ILI9488_RST_CH, _DEF_OUTPUT);
}

static bool ili9488InitRegs(void)
{
  static const uint8_t pgamma[] =
  {
    0x00, 0x03, 0x09, 0x08, 0x16,
    0x0A, 0x3F, 0x78, 0x4C, 0x09,
    0x0A, 0x08, 0x16, 0x1A, 0x0F
  };
  static const uint8_t ngamma[] =
  {
    0x00, 0x16, 0x19, 0x03, 0x0F,
    0x05, 0x32, 0x45, 0x46, 0x04,
    0x0E, 0x0D, 0x35, 0x37, 0x0F
  };

  if (writecommand(ILI9488_SWRESET) != true)
  {
    return false;
  }
  delay(10);

  if (writecommand(ILI9488_SLPOUT) != true)
  {
    return false;
  }
  delay(120);

  if ((writecommand(ILI9488_PGAMCTRL) != true) ||
      (writedata_buf(pgamma, sizeof(pgamma)) != true))
  {
    return false;
  }

  if ((writecommand(ILI9488_NGAMCTRL) != true) ||
      (writedata_buf(ngamma, sizeof(ngamma)) != true))
  {
    return false;
  }

  if ((writecommand(ILI9488_PWCTR1) != true) ||
      (writedata(0x17) != true) ||
      (writedata(0x15) != true))
  {
    return false;
  }

  if ((writecommand(ILI9488_PWCTR2) != true) ||
      (writedata(0x41) != true))
  {
    return false;
  }

  if ((writecommand(ILI9488_VMCTR1) != true) ||
      (writedata(0x00) != true) ||
      (writedata(0x12) != true) ||
      (writedata(0x80) != true))
  {
    return false;
  }

  if ((writecommand(ILI9488_FRMCTR1) != true) ||
      (writedata(0xA0) != true))
  {
    return false;
  }

  if ((writecommand(ILI9488_DFUNCTR) != true) ||
      (writedata(0x02) != true) ||
      (writedata(0x02) != true) ||
      (writedata(0x3B) != true))
  {
    return false;
  }

  if ((writecommand(ILI9488_ENTMOD) != true) ||
      (writedata(0xC6) != true))
  {
    return false;
  }

  if ((writecommand(ILI9488_ADJCTL3) != true) ||
      (writedata(0xA9) != true) ||
      (writedata(0x51) != true) ||
      (writedata(0x2C) != true) ||
      (writedata(0x82) != true))
  {
    return false;
  }

  if ((writecommand(ILI9488_PIXFMT) != true) ||
      (writedata(ILI9488_PIXFMT_18BIT) != true))
  {
    return false;
  }

  if (writecommand(ILI9488_NORON) != true)
  {
    return false;
  }
  delay(10);

  /*
   * This ILI9488 module's panel polarity is opposite to the controller's
   * reset default. Without inversion the whole TouchGFX UI is displayed as
   * a photographic negative (dark background becomes bright, white text
   * becomes black, and green becomes magenta).
   */
  if (writecommand(ILI9488_INVON) != true)
  {
    return false;
  }
  delay(10);

  if (writecommand(ILI9488_DISPON) != true)
  {
    return false;
  }
  delay(120);

  return true;
}

bool ili9488SetRotation(uint8_t mode)
{
  uint8_t madctl;

  if (writecommand(ILI9488_MADCTL) != true)
  {
    return false;
  }

  switch (mode & 0x03U)
  {
    case 0:
      madctl = MADCTL_MX | MADCTL_BGR;
      break;

    case 1:
      madctl = MADCTL_MV | MADCTL_BGR;
      break;

    case 2:
      madctl = MADCTL_MY | MADCTL_BGR;
      break;

    default:
      madctl = MADCTL_MX | MADCTL_MY | MADCTL_MV | MADCTL_BGR;
      break;
  }

  return writedata(madctl);
}

bool ili9488SetWindow(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
  x0 = constrain(x0, 0, HW_LCD_WIDTH - 1);
  x1 = constrain(x1, 0, HW_LCD_WIDTH - 1);
  y0 = constrain(y0, 0, HW_LCD_HEIGHT - 1);
  y1 = constrain(y1, 0, HW_LCD_HEIGHT - 1);

  if (x0 > x1)
  {
    int32_t tmp = x0;
    x0 = x1;
    x1 = tmp;
  }

  if (y0 > y1)
  {
    int32_t tmp = y0;
    y0 = y1;
    y1 = tmp;
  }

  spiSetBitWidth(spi_ch, 8);

  if ((writecommand(ILI9488_CASET) != true) ||
      (writedata((uint8_t)(x0 >> 8)) != true) ||
      (writedata((uint8_t)x0) != true) ||
      (writedata((uint8_t)(x1 >> 8)) != true) ||
      (writedata((uint8_t)x1) != true) ||
      (writecommand(ILI9488_PASET) != true) ||
      (writedata((uint8_t)(y0 >> 8)) != true) ||
      (writedata((uint8_t)y0) != true) ||
      (writedata((uint8_t)(y1 >> 8)) != true) ||
      (writedata((uint8_t)y1) != true) ||
      (writecommand(ILI9488_RAMWR) != true))
  {
    return false;
  }

  return true;
}

bool ili9488SendBuffer(uint8_t *p_data, uint32_t length, uint32_t timeout_ms)
{
  const uint16_t *p_color = (const uint16_t *)p_data;
  uint8_t tx_buf[ILI9488_TX_CHUNK_BYTES];
  uint32_t spi_timeout = timeout_ms;

  if ((p_data == NULL) || (length == 0U))
  {
    return false;
  }

  if (spi_timeout == 0U)
  {
    spi_timeout = ILI9488_SPI_TIMEOUT_MS;
  }

  is_write_frame = true;

  gpioPinWrite(ILI9488_DC_CH, _DEF_HIGH);
  gpioPinWrite(ILI9488_CS_CH, _DEF_LOW);

  spiSetBitWidth(spi_ch, 8);

  while (length > 0U)
  {
    uint32_t chunk_pixels = length;
    uint32_t tx_len = 0;

    if (chunk_pixels > ILI9488_TX_CHUNK_PIXELS)
    {
      chunk_pixels = ILI9488_TX_CHUNK_PIXELS;
    }

    for (uint32_t i = 0; i < chunk_pixels; i++)
    {
      uint16_t color = *p_color++;
      uint8_t r = (uint8_t)((color >> 11) & 0x1FU);
      uint8_t g = (uint8_t)((color >> 5) & 0x3FU);
      uint8_t b = (uint8_t)(color & 0x1FU);

      tx_buf[tx_len++] = (uint8_t)((r << 3) | (r >> 2));
      tx_buf[tx_len++] = (uint8_t)((g << 2) | (g >> 4));
      tx_buf[tx_len++] = (uint8_t)((b << 3) | (b >> 2));
    }

    if (spiWrite(spi_ch, tx_buf, tx_len, spi_timeout) != true)
    {
      gpioPinWrite(ILI9488_CS_CH, _DEF_HIGH);
      is_write_frame = false;
      return false;
    }

    length -= chunk_pixels;
  }

  TransferDoneISR();

  return true;
}

bool ili9488SetCallBack(void (*p_func)(void))
{
  frameCallBack = p_func;

  return true;
}

#endif
#endif
#endif
