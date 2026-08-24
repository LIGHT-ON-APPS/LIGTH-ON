# APPS bring-up guide

이 폴더는 APPS(Accelerator Pedal Position Sensor) 두 채널을 읽어서 비타당성(plausibility)을 판단하고, 정상일 때만 Bamocar D3에 CAN 토크 지령을 보내기 위한 시작점입니다.

## 전체 흐름

1. APPS 두 아날로그 출력 -> 아날로그 보호/분압 회로 -> STM32 ADC 2채널
2. ADC raw 값을 각 채널별 보정값으로 0~1000 per-mille(0.1% 단위)로 변환
3. 두 채널의 환산 페달 위치 차이가 10%p(=100 per-mille) 이상이면 APPS DIFF 검증 시작
4. 정상일 때만 페달 위치를 `0..APPS_TORQUE_MAX` 크기의 torque command로 환산
5. 양 끝을 스냅(cutoff)한 뒤 `APPS_TORQUE_COMMAND_SIGN`으로 구동 방향 부호를 적용
6. CAN standard frame, ID `0x201`, DLC `3`, payload `{0x90, torque_lsb, torque_msb}` 전송
7. 확정된 DIFF 또는 즉시 RANGE/ADC failure 상태에서는 torque command `0` 전송

## 현재 소프트웨어 구조

- `src/ap/ap.c`: `apInit()`의 초기화 호출과 `apMain()`의 실행 호출, 그리고 서비스 CLI의 개시와 주기 실행을 담당합니다.
- `src/ap/ap_runtime.c`: APPS 주기 실행, 표시 UART, 진단 CAN, Bamocar 요청을 통합 관리합니다. 모듈 초기화는 `apModuleInit()`, 런타임 상태 초기화는 `apRuntimeInit()`으로 나뉩니다.
- `src/ap/sw/apps/apps.h`, `apps.c`: APPS 구조체와 상태는 `apps.c` 내부 전역변수로 관리합니다. 외부에서는 채널, 보정값, torque command 같은 값만 함수 인자로 전달합니다.
- APPS CLI 출력과 런타임 보정 명령은 `apps.c` 안에서 같이 관리합니다.
- `src/ap/sw/bamocar/bamocar_can.h`, `bamocar_can.c`: Bamocar D3의 16-bit register write/read request CAN payload를 만드는 작은 헬퍼입니다. `apps`, `telemetry`, `ap_runtime`이 함께 씁니다.
- `src/ap/sw/telemetry`: CAN 차량 신호를 디코딩해 표시 장치에 전달하는 텔레메트리 모듈입니다.

## 먼저 확정해야 할 하드웨어

### APPS 센서

사용 예정인 KYOCERA AVX `9168100100`은 DigiKey 기준으로 two-channel Hall angle sensor, 0~45도, analog voltage output, 4.75~5.25 V supply입니다. 따라서 STM32 ADC에 직접 5 V 아날로그를 넣으면 안 됩니다. STM32L432KC의 ADC 입력은 MCU의 `VDDA` 범위를 넘기면 안 되므로, 센서는 5 V로 구동하되 각 출력은 분압 또는 버퍼 회로를 거쳐 0~3.3 V 안으로 제한해야 합니다.

추천 시작 회로:

- 센서 전원: 안정적인 5 V regulator
- 센서 GND와 MCU AGND: 같은 기준점으로 연결
- 각 신호: 센서 출력 -> RC 저역통과/보호 -> 분압 -> ADC pin
- 분압 예: `18 kOhm` series, `33 kOhm` to GND이면 5.0 V가 약 3.24 V가 됩니다.
- ADC 앞단에는 너무 큰 저항값을 쓰지 말고, CubeMX에서 ADC sampling time을 길게 잡습니다.

정확한 pinout, output curve, dual-channel 방향(같은 방향/반대 방향)은 실제 datasheet와 실측으로 확인해야 합니다. 비타당성 판단은 raw 전압끼리 비교하지 말고, 반드시 각 채널을 0~100%로 보정한 뒤 비교합니다.

### STM32 ADC 설정

현재 프로젝트의 Cube 설정에는 CAN은 켜져 있지만 ADC HAL이 아직 꺼져 있습니다. 다음 순서로 켜는 것을 추천합니다.

1. CubeMX 또는 STM32CubeIDE에서 ADC1 enable
2. USART1 `PA9/PA10`, CAN1 `PA11/PA12`, SWD `PA13/PA14`와 겹치지 않는 ADC 가능 핀 2개 선택
3. ADC resolution 12-bit, scan conversion 2 ranks
4. 처음에는 polling으로 읽고, 값이 안정되면 DMA circular로 변경
5. 부팅 시 `HAL_ADCEx_Calibration_Start()` 후 측정 시작

ADC raw 보정은 실제 페달을 놓은 상태와 끝까지 밟은 상태에서 각 채널 raw를 기록해서 채웁니다.

```c
appsInit();
appsSetConfig(520, 3540, 3480, 470);
```

위 예시처럼 2번 채널이 반대 방향이면 `raw_min > raw_max`로 넣어도 됩니다.

## Bamocar CAN 쪽 핵심

STM32의 CAN RX/TX 핀은 CAN bus가 아니라 로직 신호입니다. 반드시 CAN transceiver가 필요합니다.

- STM32 CAN1 RX/TX: 현재 Cube 설정상 `PA11/PA12`
- 권장 transceiver: 3.3 V MCU 로직과 호환되는 ISO 11898-2 CAN transceiver
- Bamocar X9 CAN connector: `X9:3 CAN-GND`, `X9:4 CAN-H`, `X9:5 CAN-L`
- CAN-H/CAN-L 양 끝단에만 120 Ohm termination
- Bamocar 기본 CAN 설정: Rx ID `0x201`, Tx ID `0x181`, 500 kbaud

Bamocar CAN manual 기준으로 master -> controller frame은 byte 0이 register ID, byte 1부터 little-endian data입니다. Torque command register는 `0x90`이고, 명령은 signed 16-bit로 100% 크기가 `32767`입니다. 예를 들어 크기 50%는 `16380 = 0x3FFC`라서 payload가 `{0x90, 0xFC, 0x3F}`가 됩니다.

### 구동 방향 부호

초기 시험에서 차가 반대 방향으로 달려서, 현재는 소프트웨어에서 토크 명령의 부호를 뒤집어 정방향을 맞추고 있습니다. 방향은 `apps.h`의 매크로 하나로 결정됩니다.

```c
/* -1 이면 모터 방향 반전, +1 이면 원래 방향 */
#define APPS_TORQUE_COMMAND_SIGN           (-1)
```

따라서 **현재 빌드에서 정방향 가속은 음수 명령**입니다. 페달 100%는 `-32767`이고, 크기 50%는 `-16380 = 0xC004`라서 payload가 `{0x90, 0x04, 0xC0}`가 됩니다.

부호는 `appsSendCommand()`의 CAN 송신 직전에서 딱 한 번만 곱합니다. 페달 환산, cutoff 판정, plausibility 판단은 모두 부호 없는 크기로 계산하므로 방향을 바꿔도 이 로직들은 그대로입니다. 진단 CAN(`0x7A1`)과 표시 UART의 `command` 필드는 signed 16-bit라서 음수 값이 그대로 실려 나갑니다.

이 방식은 임시방편입니다. 인버터 입장에서는 역방향 토크 영역을 쓰고 있는 것이므로, Bamocar의 정/역 토크 리미트가 비대칭이면 페달 100%에서도 토크가 덜 나올 수 있습니다. 근본 수정은 모터 3상 중 2상 교환 + 리졸버 방향 보정, 또는 NDrive의 회전 방향 파라미터 반전입니다. 그쪽으로 옮기면 이 매크로를 `+1`로 되돌립니다.

## CLI가 출력되지 않았던 원인과 복구 방법

### 최종 원인

CLI와 UART 코드가 실행되지 않은 직접 원인은 ADC 추가가 아니라 MCU의 부팅 모드였습니다.

STM32가 사용자 Flash의 `Reset_Handler`가 아닌 시스템 부트로더 영역에서 실행되고 있었습니다.

```text
정상 실행 주소: 0x0800xxxx (Main Flash)
문제 발생 주소: 0x1FFFxxxx (System Memory Bootloader)
```

당시 옵션 바이트는 `nSWBOOT0=1`이어서 BOOT0 값을 물리 핀에서 읽도록 되어 있었습니다. BOOT0 핀이 High로 인식되면서 시스템 부트로더로 진입했고, 그 결과 다음 현상이 발생했습니다.

- `main()`이 실행되지 않음
- `hwInit()`, `apInit()`이 실행되지 않음
- USART2 레지스터가 초기화되지 않음
- Tera Term에 `cli#` 프롬프트가 출력되지 않음
- 새 펌웨어를 정상적으로 Flash에 기록해도 애플리케이션이 시작되지 않음

ADC 설정을 추가한 시점과 증상 발생 시점이 겹쳤지만, ADC 변환이나 ADC 핀 자체가 CLI를 막은 것은 아니었습니다.

### 적용한 복구 설정

BOOT0 물리 핀 상태를 사용하지 않고 Main Flash로 부팅하도록 옵션 바이트를 변경했습니다.

```text
nSWBOOT0 = 0  // BOOT0 값을 옵션 바이트에서 선택
nBOOT0   = 1  // BOOT0 = 0, Main Flash 부팅
```

STM32CubeProgrammer CLI 명령:

```powershell
STM32_Programmer_CLI.exe -c port=SWD -ob nSWBOOT0=0 nBOOT0=1
STM32_Programmer_CLI.exe -c port=SWD -rst
```

설정 후 PC가 `0x0800xxxx` 영역에서 실행되는지 확인합니다.

```powershell
STM32_Programmer_CLI.exe -c port=SWD -score -coreReg PC LR MSP
```

### CLI 점검 순서

1. Tera Term에서 ST-Link Virtual COM Port 선택
2. `57600 baud`, `8 data bits`, `none parity`, `1 stop bit`, `flow control none`
3. 최신 `Debug/stm32l432kc.elf`를 Flash에 기록
4. PC가 `0x0800xxxx`에서 실행되는지 확인
5. Tera Term에서 Enter 입력

PC가 `0x1FFFxxxx`이면 UART 코드를 수정하기 전에 BOOT0/옵션 바이트부터 확인해야 합니다.

### UART 관련 참고

CLI 프롬프트가 출력되지만 입력이 동작하지 않는다면 그때는 UART RX 경로를 확인합니다.

- `PA2`: USART2 TX
- `PA15`: USART2 RX
- USART2 IRQ가 활성화되어 있는지 확인
- `HAL_UART_Receive_IT()` 또는 RX DMA 시작 결과 확인
- `USART2_IRQHandler()`에서 `HAL_UART_IRQHandler(&huart2)` 호출 확인

프롬프트 자체가 전혀 출력되지 않는 경우에는 UART RX보다 애플리케이션 부팅 여부를 먼저 확인하는 것이 빠릅니다.

상위 제어 루프에서는 `appsRunPedalCommand()`를 주기적으로 호출하기만 하면 됩니다. 토크 명령을 밖에서 만들어 넘기지 않고, 함수 안에서 페달 위치로부터 직접 만듭니다.

```c
/* ap_runtime.c 에서 APPS_RUN_MS(10 ms) 주기로 호출 */
(void)appsRunPedalCommand();
```

`appsRunPedalCommand()`는 ADC 읽기, 페달 위치의 토크 명령 환산, 방향 부호 적용, CAN 전송까지 한 호출 안에서 처리합니다. `valid == false`이면 페달 위치와 관계없이 항상 0을 전송합니다.

`apps.c`는 이상 원인을 두 종류로 나눠 처리합니다. 두 채널의 환산 페달 위치 차이(`DIFF`)는 규정상 최대 100 ms 안에 failure로 전환하고, ADC 읽기 실패나 각 채널의 유효 raw 범위 이탈(`RANGE`)은 첫 제어 주기부터 즉시 failure로 처리합니다.

## APPS fault 지속시간 계산

`DIFF >= 10.0%`의 규정상 최대 시간은 `100 ms`입니다. 별도의 90 ms 보정값은 사용하지 않습니다. TIM6가 1 kHz로 시작하는 ADC 변환 완료 인터럽트에서 DIFF를 매 샘플 확인하고, 연속 100개 샘플에서 DIFF가 유지되면 failure로 확정합니다. RANGE/ADC failure에는 이 타이머를 적용하지 않습니다.

```c
#define ADC_SAMPLE_HZ               1000U
#define APPS_FAULT_MS               100U
#define APPS_DIFF_CONFIRM_SAMPLES   100U
```

ADC 인터럽트는 첫 DIFF 샘플을 1개로 세고, 이후 연속 샘플마다 카운트를 증가시킵니다. 1 kHz에서 100개 연속 샘플은 약 100 ms 판정창에 해당합니다.

```c
if (difference_per_mille >= APPS_DIFF_MAX)
{
  bad_sample_count++;
  if (bad_sample_count >= APPS_DIFF_CONFIRM_SAMPLES)
  {
    fault_latched = true;
  }
}
else
{
  bad_sample_count = 0U;
}
```

각 값의 의미는 다음과 같습니다.

- `ADC_SAMPLE_HZ`: APPS ADC 샘플링 주파수 `1000 Hz`
- `bad_sample_count`: 연속으로 DIFF가 검출된 ADC 샘플 수
- `APPS_FAULT_MS`: 규정에 명시된 최대 비타당성 지속시간 `100 ms`
- `APPS_DIFF_CONFIRM_SAMPLES`: 100 ms 판정창에 해당하는 연속 ADC 샘플 수
- `fault_latched`: 현재 모터 출력을 차단해야 하는 failure가 활성 상태인지 나타내는 호환 필드

DIFF가 99개 샘플까지만 유지되면 pending이고, 100번째 연속 DIFF 샘플에서 failure로 확정됩니다. 샘플 사이에 정상 샘플이 하나라도 들어오면 카운트는 즉시 0으로 초기화됩니다.

```text
DIFF 99 samples  → PENDING
DIFF 100 samples → FAILURE, READY LOW
```

### 100개 샘플 전에 정상으로 복구되는 경우

DIFF가 100개 연속 샘플 전에 사라지면 카운터가 0으로 초기화되며 failure로 확정되지 않습니다.

```text
DIFF 발생 → 60 ms 지속 → 정상 복구
fault timer 초기화
DIFF failure 발생하지 않음
```

이후 다시 DIFF가 발생하면 이전 카운트에 이어서 계산하지 않고 첫 샘플부터 새로 측정합니다. RANGE/ADC 비트가 DIFF와 동시에 추가되거나 해제되어도 DIFF 자체가 계속 존재하는 한 카운터는 초기화되지 않습니다.

### CAN command 동작

DIFF와 RANGE/ADC failure의 CAN 동작은 서로 다릅니다.

```text
정상                                  → 페달 명령 전송
DIFF 연속 1..99 samples                → PENDING, 페달 명령 전송
DIFF 연속 100 samples                  → ISR에서 failure, READY 즉시 LOW
다음 APPS 메인 제어 실행               → command 0 전송
DIFF가 100 samples 전에 완전히 사라짐 → 카운터 초기화
RANGE failure 첫 ADC 샘플              → ISR에서 failure, READY 즉시 LOW
다음 APPS 메인 제어 실행               → command 0 전송
failure 활성 중 다른 이상이 남음       → command 0 유지
모든 APPS 이상이 사라짐                → 정상 명령 전송 재개
```

중요: RANGE 즉시 failure는 단선된 ADC 입력이 반드시 유효 raw 범위 밖으로 이동할 때만 단선을 검출합니다. 현재 보드처럼 ADC 입력이 부동인 상태에서 단선값이 93% 같은 정상 범위에 머물고 다른 채널과의 차이까지 10% 미만이 되면, 두 ADC 값만으로는 정상 입력과 구분할 수 없습니다. 현재 유효 하한도 `0`이므로 단순 풀다운은 `raw=0`을 정상 범위로 만들 수 있습니다. 단선 시 3.3 V 쪽 범위 밖으로 확실히 이동하도록 외부 풀업을 적용하고, 실측한 정상 최대값과 단선 최저값 사이에 `VALID_RAW_MAX`를 설정·검증해야 합니다.

정상 구간에서도 페달 명령이 그대로 나가지는 않고, 양 끝이 스냅됩니다.

```c
#define APPS_TORQUE_CUTOFF                 1500   /* 약 4.6%, 이하는 0 */
#define APPS_TORQUE_CUTOFF_HIGH            32112  /* 약 98%, 이상은 100% */
```

아래쪽 cutoff는 페달을 놓았을 때 ADC 노이즈가 토크 요청으로 남지 않게 하고, 위쪽 cutoff는 기구 공차 때문에 완전히 밟아도 보정 최대치에 못 미치는(실측 99.1%) 상황에서 운전자가 100%를 낼 수 있게 합니다. 두 판정 모두 부호를 곱하기 전의 크기 기준이며, 결과는 `apps trace`의 `CUT` / `FULL` 상태로 확인할 수 있습니다.

DIFF 시간 판정은 1 kHz ADC 인터럽트에서 정수 카운터와 GPIO/flag 갱신만 수행합니다. ISR에서 대기 가능성이 있는 CAN API는 호출하지 않습니다. 따라서 100번째 DIFF 샘플에서 READY는 바로 LOW가 되지만 CAN command 0은 다음 `appsRunPedalCommand()`에서 전송됩니다. READY가 실제 인버터 enable/torque gate를 차단하지 않는 단순 진단선이라면 CAN 전송·중재·인버터 반응 지연까지 포함한 100 ms 규정 충족은 이 코드만으로 보장되지 않습니다. 경기용 빌드에서는 blocking CLI를 비활성화하고 APPS 물리 고장 시점부터 READY LOW 및 실제 CAN 0 도달시간을 각각 계측해야 합니다.

STM32CubeIDE managed build를 쓰면 새 하위 폴더가 자동 빌드 목록에 들어가도록 project refresh 또는 makefile regeneration이 필요할 수 있습니다.

## 검증 순서

1. 센서만 전원 인가하고 multimeter로 두 출력 전압 범위 확인
2. MCU ADC raw를 CLI로 출력해서 페달 release/full raw 기록
3. `apps info` 또는 `apps show`로 두 채널 per-mille과 fault 상태 확인
4. CAN transceiver + USB-CAN으로 ID `0x201`, DLC `3`, payload 확인
5. Bamocar는 고전압/모터 enable 없이 먼저 bus 수신 확인
6. NDrive에서 command mode가 digital torque command를 받을 수 있게 설정되었는지 확인
7. 바퀴가 떠 있거나 동력 전달이 끊긴 상태에서 페달을 살짝 밟아 작은 명령부터 시험. 현재 빌드의 구동 방향은 음수이므로 이때 나가는 값은 작은 negative command입니다
8. 바퀴가 도는 방향을 반드시 눈으로 확인. 방향이 반대라면 `APPS_TORQUE_COMMAND_SIGN`을 뒤집기 전에 인버터 회전 방향 파라미터와 결선 쪽 수정을 먼저 검토합니다
9. 정/역 토크 리미트가 대칭인지 NDrive에서 확인. 음수 쪽 리미트가 낮으면 페달 100%에서도 토크가 덜 나옵니다
10. 회생제동은 배터리/전원장치가 braking energy를 흡수할 수 있는지 확인한 뒤 시험. 구동 부호가 음수인 현재 빌드에서 회생 방향은 positive command입니다

## 브레이크 유압센서 추가 시

브레이크 압력은 별도 ADC 채널로 읽고, APPS와 동일하게 범위/단선/단락 fault를 먼저 만든 뒤 torque command에 합산합니다.

합산은 부호 없는 크기끼리 하고, 방향 부호는 지금처럼 CAN 송신 직전에 한 번만 곱합니다.

```c
net_magnitude = accel_magnitude - regen_magnitude;
net_cmd       = net_magnitude * APPS_TORQUE_COMMAND_SIGN;
```

이때 `appsSendCommand()`의 cutoff 판정을 함께 개정해야 합니다. 현재 판정은 `command_to_send > 0`으로 양수 크기만 가정하고 있어서, 회생이 가속을 넘어 `net_magnitude`가 음수가 되는 구간을 그대로 통과시킵니다.

초기 안전 정책은 APPS fault 또는 brake sensor fault 중 하나라도 있으면 `net_cmd = 0`으로 두는 것을 추천합니다. 회생제동까지 안전하게 허용하려면 brake sensor의 별도 plausibility와 Bamocar/BMS의 DC bus overvoltage 대응이 먼저 검증되어야 합니다.

## 참고 자료

- APPS 후보: [DigiKey KYOCERA AVX 9168100100](https://www.digikey.com/en/products/detail/kyocera-avx/9168100100/10491367)
- Bamocar hardware manual: [UniTek BAMOCAR-PG-D3-400/400 manual](https://www.unitek-industrie-elektronik.de/wp-content/uploads/BAMOCAR-PG-D3-700-400_EN.pdf)
- Bamocar CAN manual: [UniTek CAN-BUS manual](https://www.unitek-industrie-elektronik.de/wp-content/uploads/CAN_EN.pdf)
- NDrive software manual: [UniTek NDrive manual](https://www.unitek-industrie-elektronik.de/wp-content/uploads/NDrive_EN.pdf)
