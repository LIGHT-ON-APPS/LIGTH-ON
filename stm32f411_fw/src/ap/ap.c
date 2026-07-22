/*
 * ap.c
 *
 *  Created on: Dec 6, 2020
 *      Author: baram
 */


#include "ap.h"
#include "display_link.h"

#ifdef _USE_HW_TOUCHGFX
#include "app_touchgfx.h"
#endif

void apInit(void)
{
  cliOpen(_DEF_UART2, 57600);
  (void)displayLinkInit();

#ifdef _USE_HW_TOUCHGFX
  MX_TouchGFX_Init();
#endif
}

void apMain(void)
{
  uint32_t pre_time;

  pre_time = millis();
  while(1)
  {
    displayLinkUpdate();

    if (millis()-pre_time >= 500)
    {
      pre_time = millis();
      ledToggle(_DEF_LED1);
    }

#ifdef _USE_HW_TOUCHGFX
    MX_TouchGFX_Process();
#endif

    cliMain();
  }
}
