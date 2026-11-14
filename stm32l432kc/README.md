# APPS 펌웨어 설정 및 동작

이 문서는 현재 `stm32l432kc/src`의 활성 코드 기준으로 입력·고장 정책·출력·진단 방법을 정리합니다. 프로젝트 개요는 [최상위 README](../README.md), 초기 bring-up과 부팅 문제 해결 기록은 [개편 전 문서 원본](README.before-portfolio-2026-11-14.md)을 참고합니다. 원본은 이력 보존용이며 현재 설정 안내와 구분합니다.

## 초기화와 실행

`apInit()`은 `apModuleInit()` → `apRuntimeInit()` → CLI 초기화 순서로 실행합니다. 메인 루프는 `apRuntimeUpdate()` 뒤 `cliMain()`을 호출합니다. 현재 코드는 CLI를 직접 열며 별도 service mode 진입을 요구하지 않습니다.

| 실행 경로 | 주기·역할 |
| --- | --- |
| TIM6 → ADC1 → DMA 완료 콜백 | 1 kHz, 두 채널 범위·DIFF 검사 |
| `apAppsUpdate()` | 10 ms, ADC 생존 감시·페달 계산·토크 명령 |
| 표시 상태 UART | 50 ms |
| 진단 CAN | 50 ms, 3프레임 |
| 표시 telemetry UART | 100 ms |
| Bamocar 주기 송신 재요청 | 1000 ms, 요청 간격 필드 100 ms |

위 값은 스케줄러 설정입니다. 메인 루프 작업 지연이 없는 것을 보증하는 실측 주기와는 구분합니다.

## ADC와 채널 보정

ADC HAL은 활성화되어 있습니다. 활성 드라이버는 [adc.c](src/hw/driver/adc.c)이며, 12-bit ADC1, TIM6 트리거, circular DMA를 사용하고 시작 시 ADC 보정을 수행합니다. `adcUpdate()` / `adcRead()`는 이 측정 경로의 결과를 사용합니다. polling에서 DMA로 옮겨야 하는 초기 단계가 아닙니다.

| APPS 채널 | 논리 ADC 채널 | MCU 입력 | 0% raw | 100% raw | 유효 raw |
| --- | --- | --- | ---: | ---: | --- |
| APPS1 | `_DEF_ADC2` | PA1 / ADC1_IN6 | 610 | 3700 | 200~4050 |
| APPS2 | `_DEF_ADC1` | PA0 / ADC1_IN5 | 458 | 2812 | 200~4050 |

환산은 `(raw - raw_min) × 1000 / (raw_max - raw_min)`이며 결과를 0~1000으로 제한합니다. 최종 페달은 두 환산값 중 작은 값입니다. 입력이 보정 범위를 벗어난 것과 전기적 유효 범위를 벗어난 것은 서로 다른 조건입니다.

현재 공개 API는 `appsInit()`, `appsGetSnapshot()`, `appsRunPedalCommand()`입니다. **`appsSetConfig()`와 런타임 보정 setter는 없습니다.** 보정값은 [apps.h](src/ap/sw/apps/apps.h)의 `APPS_SIGNAL*_RAW_*_DEFAULT`, 유효 범위는 `APPS_SIGNAL*_VALID_RAW_*_DEFAULT`를 변경하고 다시 빌드합니다. `apps config`는 설정을 출력하는 명령입니다.

센서 공급 전압과 MCU ADC 허용 입력은 구분해야 합니다. 실제 보드의 분압·보호·입력 바이어스와 raw 실측값을 대조한 뒤 보정합니다. 현재 소스만으로 센서 모델이나 회로 실장을 확정하지 않습니다.

## 고장 판정

### DIFF: 연속 샘플로 확정

보정된 두 채널의 차이가 `APPS_DIFF_MAX = 100` 이상일 때 DIFF입니다. 단위는 per-mille이므로 10%p 차이에 해당합니다.

```text
ADC_SAMPLE_HZ = 1000 Hz
APPS_FAULT_MS = 100 ms
APPS_DIFF_CONFIRM_SAMPLES = ceil(100 × 1000 / 1000) = 100

연속 1~99개 DIFF 샘플 → pending
100번째 DIFF 샘플    → 확정 고장, READY LOW
DIFF 해소           → DIFF 연속 카운터 초기화
```

첫 검출 샘플도 1개로 셉니다. 첫 샘플과 100번째 샘플 사이의 시간은 명목상 99 ms이며, 물리 고장 시점과 첫 샘플의 위상 차이도 고려해야 합니다. 100샘플 정책을 실제 인버터 토크 차단 지연으로 치환하지 않습니다.

### RANGE·ADC: 지연 판정 없이 처리

- raw가 유효 범위 밖이면 첫 검출 콜백에서 고장을 처리합니다.
- ADC 읽기 실패는 검출 경로에서 즉시 처리합니다.
- ADC ISR sequence가 갱신되지 않은 기간은 메인에서 확인합니다. 기준은 `APPS_ADC_STALE_MS = 10 ms`이며 실제 검출 시점은 메인 실행 시점에 달려 있습니다.
- ISR에서 고장 확정 시 READY를 LOW로 내리고, 메인이 아직 처리하지 않은 확정 이벤트를 보존합니다.

메인은 관측 상태·확정 고장·미처리 이벤트를 합쳐 출력 가능 여부를 결정합니다. `fault_latched_status`는 현재 적용 중인 고장 원인 비트마스크입니다. 별도 `bool fault_latched` 필드는 없으며, `fault_latched_status != APPS_STATUS_OK`로 고장 여부를 판단합니다. 수동 reset 전까지 영구 유지되는 래치를 뜻하지 않고, 확정 고장 이벤트 처리 후 모든 APPS 이상이 해소되면 정상 명령으로 복귀할 수 있습니다.

### 단선 검출의 조건

현재 유효 raw 범위는 두 채널 모두 `200~4050`이므로 raw 0은 범위 이탈입니다. 다만 단선 시 입력이 부동으로 남아 이 범위 안에 들어오면 RANGE만으로는 단선을 구분할 수 없습니다. 풀업·풀다운의 선택과 저항값은 실제 회로에서 정상 범위와 단선값을 측정해 결정해야 합니다.

## 토크 명령과 출력 버전

기본 크기는 `pedal_per_mille × 32767 / 1000`입니다.

| 설정 | 현재 값 | 의미 |
| --- | ---: | --- |
| `APPS_TORQUE_CUTOFF` | 1500 | 양수 크기 1500 이하를 0으로 처리 |
| `APPS_TORQUE_CUTOFF_HIGH` | 32767 | 최대값에서만 full 처리; 98% 조기 포화 없음 |
| `APPS_TORQUE_COMMAND_SIGN` | -1 | 송신 직전에 방향 부호 반전 |
| `_DEF_VER` | `_DEF_VER2` | 무효 입력 시 토크 0 송신 분기 |

현재 `appsSendCommand()`에는 `_DEF_VER1`의 CAN close/mute 분기와 `_DEF_VER2`의 토크 0 송신 분기가 있습니다. 따라서 모든 버전이 고장 시 CAN 0을 송신한다고 설명하면 안 됩니다.

기본 버전에서 유효 입력의 페달 100%는 `-32767`이고 CAN payload는 `{0x90, 0x01, 0x80}`입니다. 정확히 50%인 `500 per-mille`는 정수 나눗셈에 의해 크기 `16383`, 송신값 `-16383`, payload `{0x90, 0x01, 0xC0}`가 됩니다.

CAN은 standard ID `0x201`, DLC `3`, register `0x90`과 little-endian signed 16-bit 명령을 사용합니다. 응답 ID는 `0x181`, 버스 설정은 500 kbit/s입니다. 방향 부호의 물리적 의미는 실제 인버터 설정·결선과 대조합니다. 현재 코드에는 브레이크 압력 기반 회생제동 합산 기능이 없습니다.

READY HIGH는 유효한 APPS 상태, 최신 토크 CAN TX 성공, mute 아님, ISR 출력 금지 아님을 함께 요구합니다. **물리 고장 → READY LOW → CAN 0 도달 → 실제 토크 차단**의 전체 시간은 하드웨어에서 별도 측정해야 합니다. ISR 안에서는 CAN API를 호출하지 않습니다.

## CLI와 통신

| 용도 | 실제 주변장치·설정 |
| --- | --- |
| CLI `_DEF_UART1` | USART2, PA2 TX / PA15 RX, 57600 baud, 8-N-1 |
| 표시 송신 `_DEF_UART2` | USART1 TX, PA9, 115200 baud |
| CAN | PA11 RX / PA12 TX, 별도 CAN 트랜시버 필요 |

현재 유효한 APPS CLI 명령은 다음과 같습니다.

```text
apps status   # 상태 1회 출력
apps config   # 보정값·임계값·ADC 설정 출력
apps monitor  # 상태 반복 출력
apps verify   # 타당성 진단 모드
apps trace    # 페달·토크 cutoff 추적
apps help
```

반복 명령의 출력·대기 중에는 메인 제어 실행이 지연될 수 있으므로 시간 검증 시 별도로 다룹니다. 과거 문서의 `apps info` / `apps show`는 현재 APPS CLI 명령이 아닙니다.

표시 UART 송신 코드는 남아 있지만 현재 DISPLAY 저장소는 CAN 직접 수신 방식입니다. APPS 진단은 [공유 헤더](../shared/apps_diag_can.h)의 CAN `0x7A0~0x7A2`를 사용합니다.

## 빌드 및 검증 항목

STM32CubeIDE에서 이 폴더의 기존 프로젝트를 가져와 빌드하고 ST-Link/SWD로 기록합니다. CubeMX 생성 `src/lib/cube_l432/Core`는 활성 드라이버와 구분합니다. DMA를 변경할 때는 [설정 체크리스트](DMA_SETUP_CHECKLIST.md)의 활성 소스·IRQ 반영 절차를 확인합니다.

다음은 검증 시나리오이며, 실행 완료된 시험 성적을 의미하지 않습니다.

| 입력·조건 | 확인할 내용 |
| --- | --- |
| 페달 release/full | 두 채널 raw·per-mille·전송 명령 |
| DIFF 99 / 100 연속 샘플 | pending과 확정 경계 |
| DIFF 도중 정상 샘플 | 연속 카운터 초기화 |
| raw 200 미만 / 4050 초과 | RANGE 검출, READY LOW |
| ADC 갱신 정지 | stale 검출과 출력 금지 |
| CAN 송신 실패 | TX fault와 READY 조건 |
| 모든 APPS 이상 해소 | 확정 이벤트 처리 후 정상 복귀 |
| 물리 센서 고장 | READY·CAN 0·실제 인버터 응답 지연 계측 |

펌웨어 기록 후 CLI가 전혀 없으면 UART 수정 전에 사용자 Flash가 실행 중인지와 BOOT 설정을 확인합니다. 이전 장애 당시의 관측과 복구 명령은 [초기 bring-up 기록 원본](README.before-portfolio-2026-11-14.md)에 보존했습니다.
