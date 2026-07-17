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
      (buttonInit() != true) ||
      (flashInit() != true) ||
      (ymodemInit() != true) ||
      (usbBegin(USB_CDC_MODE) != true))
  {
    Error_Handler();
  }
}
