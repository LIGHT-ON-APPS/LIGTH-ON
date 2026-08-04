/*
 * ap.c
 *
 * 애플리케이션 진입점. 런타임 통합은 ap_runtime.c에 두고,
 * 하위 소프트웨어 모듈은 src/ap/sw에서 관리한다.
 */

#include "ap.h"

#include "ap_runtime.h"


/* 애플리케이션 런타임을 한 번 초기화한다. */
void apInit(void)
{
  apRuntimeInit();
}

/* 메인 루프에서 런타임 작업을 계속 실행한다. */
void apMain(void)
{
  while (1)
  {
    apRuntimeUpdate();
  }
}
