/*
 * ap_runtime.h
 *
 * 애플리케이션 런타임 초기화와 주기 실행 진입점.
 *
 * 초기화는 두 단계로 나뉜다. apModuleInit()이 src/ap/sw의 소프트웨어 모듈을
 * 세우고, apRuntimeInit()이 런타임의 주기·순번·연결 상태를 잡는다.
 * 순서를 바꾸면 안 된다.
 */

#ifndef SRC_AP_AP_RUNTIME_H_
#define SRC_AP_AP_RUNTIME_H_


void apModuleInit(void);
void apRuntimeInit(void);
void apRuntimeUpdate(void);


#endif /* SRC_AP_AP_RUNTIME_H_ */
