/*
 * ap.h
 *
 *  Created on: Dec 6, 2020
 *      Author: baram
 */

#ifndef SRC_AP_AP_H_
#define SRC_AP_AP_H_

#include "hw.h"


void apInit(void);
void apMain(void);
void apSetServiceMode(bool enabled);
bool apIsServiceMode(void);

/* 진단 CAN 발행 통계. tx_total은 시도한 세트 수, tx_fail은 세 프레임 중 하나라도
 * 실패한 세트 수. CAN이 닫혀 있어 건너뛴 주기는 둘 다 세지 않는다. */
void apDiagCanGetStats(uint32_t *p_tx_total, uint32_t *p_tx_fail);


#endif /* SRC_AP_AP_H_ */
