/*
 * hw.c
 *
 *  Created on: Dec 6, 2020
 *      Author: baram
 */


#include "hw.h"


void hwInit(void)
{
  bspInit();

  if ((cliInit() != true) ||
      (ledInit() != true) ||
      (usbInit() != true) ||
      (uartInit() != true) ||
      (usbBegin(USB_CDC_MODE) != true) ||
      (gpioInit() != true) ||
      (spiInit() != true) ||
      (lcdInit() != true))
  {
    Error_Handler();
  }
}
