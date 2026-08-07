/*
 * ap.c
 *
 * 애플리케이션 진입점. 런타임 통합은 ap_runtime.c에 두고,
 * 하위 소프트웨어 모듈은 src/ap/sw에서 관리한다.
 * 서비스 CLI는 런타임에 속하지 않으므로 여기서 직접 연다.
 */

#include "ap.h"
#include "ap_runtime.h"

#ifdef _USE_HW_CLI
#include "cli.h"
#endif


/* 소프트웨어 모듈, 런타임 상태, CLI 순서로 초기화한다. 순서를 바꾸면 안 된다. */
void apInit(void)
{
  apModuleInit();
  apRuntimeInit();

#ifdef _USE_HW_CLI
  (void)cliOpen(_DEF_UART1, 57600);
#endif
}

/* 주기 제어와 통신을 먼저 처리하고, 남는 시간에 CLI 입력을 받는다. */
void apMain(void)
{
  while (1)
  {
    apRuntimeUpdate();
#ifdef _USE_HW_CLI
    cliMain();
#endif
  }
}
