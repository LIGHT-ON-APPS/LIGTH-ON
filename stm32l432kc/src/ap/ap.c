/*
 * ap.c
 *
 *  Created on: Dec 6, 2020
 *      Author: baram
 */


#include "ap.h"
#include "apps/apps.h"
#include "signal_scope/signal_scope.h"
#include "telemetry/telemetry.h"


void apInit(void)
{
#ifdef _USE_HW_CLI
  cliOpen(_DEF_UART1, 57600);
#endif

  appsInit();
  scopeInit();

//  telemetryInit();
//  guiInit();
}

void apMain(void)
{
  uint32_t apps_pre_time;

  apps_pre_time = millis();

  while(1)
  {

#ifdef _USE_HW_CLI
    cliMain();
#endif

    if (millis()-apps_pre_time >= APPS_RUN_MS)
    {
      apps_pre_time = millis();
      (void)appsRunPedalCommand();
    }


    (void)canUpdate();

    scopeUpdate();
//    telemetryUpdate();
  }
}
