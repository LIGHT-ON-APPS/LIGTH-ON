# STM32L432 DMA 추가 체크리스트

이 프로젝트에서 CubeMX의 `Generate Code`는 DMA를 실제 펌웨어에 자동 적용하지 않는다.
`src/lib/cube_l432/Core`가 빌드에서 제외되어 있으므로, 생성된 코드는 설정 참고 원본으로 사용하고 필요한 부분을 활성 소스로 옮겨야 한다.

## 실제 예제: USART1 TX DMA를 처음부터 추가하는 방법

현재 프로젝트의 목표는 다음과 같다.

- 논리 채널 `_DEF_UART1`: 실제 USART2, CLI, blocking TX 유지
- 논리 채널 `_DEF_UART2`: 실제 USART1, F411 표시 패킷, DMA TX 사용
- USART2 CLI RX만 기존 `HAL_UART_Receive_IT()` 방식 유지
- USART1 표시 링크는 단방향 TX 전용이며 RX 핀과 RX DMA를 사용하지 않음

주의: `_DEF_UART1` service CLI를 열면 PA2가 USART2 TX alternate function으로 바뀐다. PA2를 PCB GPIO로 사용하는 하드웨어에서는 GPIO 방향과 초기값을 확정하기 전까지 service mode를 활성화하지 않는다. 현재 애플리케이션은 service mode 기본값이 `false`이고 외부 진입 호출자를 아직 연결하지 않았다.

아래 순서는 CubeMX에서 USART1 TX DMA를 새로 추가했다고 가정한 실제 작업 순서다.

### 1. CubeMX에서 USART1과 DMA를 설정한다

1. `src/lib/cube_l432/cube_l432.ioc`를 STM32CubeMX 또는 CubeIDE Device Configuration Tool로 연다.
2. 왼쪽 `Connectivity > USART1`을 선택한다.
3. `Mode`를 `Asynchronous`로 설정한다.
4. Pinout에서 다음 핀이 잡혔는지 확인한다.
   - `PA9 = USART1_TX`
   - `PA10`은 USART1 RX로 할당하지 않음
5. `Parameter Settings`에서 양쪽 MCU와 동일하게 설정한다.
   - Baud rate: `115200`
   - Word length: `8 Bits`
   - Parity: `None`
   - Stop bits: `1`
   - Hardware flow control: `None`
6. `DMA Settings > Add`를 눌러 `USART1_TX`를 추가한다.
7. L432의 현재 할당값이 아래와 같은지 확인한다.

| CubeMX 항목 | 현재 값 | 의미 |
|---|---:|---|
| DMA request | `USART1_TX` | 어떤 peripheral 요청이 DMA를 발생시키는지 지정 |
| Channel | `DMA1 Channel 4` | 실제 DMA 하드웨어 채널 |
| Request | `Request 2` | L432 DMAMUX/request 선택값 |
| Direction | Memory to Peripheral | RAM의 TX 버퍼를 USART TDR로 전송 |
| Peripheral increment | Disable | USART TDR 주소는 매 byte 동일 |
| Memory increment | Enable | TX 버퍼 주소는 byte마다 증가 |
| Peripheral/Memory width | Byte/Byte | UART 데이터와 `uint8_t` 버퍼 폭 일치 |
| Mode | Normal | 한 패킷 전송 후 DMA 종료 |
| DMA priority | Low | DMA 채널끼리 버스 사용 순위 |

8. `NVIC Settings`에서 다음 두 인터럽트를 켠다.
   - `DMA1 Channel 4 global interrupt`
   - `USART1 global interrupt`
9. 현재 제어 우선순위 정책에 맞춰 둘 다 preemption priority `6`으로 둔다.

`DMA priority`와 `NVIC priority`는 서로 다른 값이다. DMA priority는 DMA 채널끼리의 하드웨어 중재이고, NVIC priority는 CPU가 어느 ISR을 먼저 실행할지 결정한다.

### 2. Generate Code 후 생성 파일에서 무엇을 찾아야 하는지 확인한다

Generate Code를 누르면 보통 아래 파일이 갱신된다.

- `Core/Src/usart.c`: `hdma_usart1_tx`, DMA 필드 설정, `__HAL_LINKDMA()`
- `Core/Src/dma.c`: DMA clock과 DMA IRQ NVIC 설정
- `Core/Src/stm32l4xx_it.c`: 실제 DMA IRQ handler
- `Core/Inc/stm32l4xx_it.h`: IRQ handler prototype

이 프로젝트에서는 `src/lib/cube_l432/Core`가 빌드 제외 상태라서 여기까지만 하면 실제 펌웨어는 바뀌지 않는다. 생성된 코드를 아래 활성 파일에 수동으로 옮겨야 한다.

### 3. 활성 `uart.c`에 DMA handle과 MSP 설정을 옮긴다

먼저 IRQ handler가 접근할 수 있도록 전역 handle을 추가한다.

```c
UART_HandleTypeDef huart1;
DMA_HandleTypeDef  hdma_usart1_tx;
```

`static`으로 선언하지 않는 이유는 `src/bsp/stm32l4xx_it.c`에서 `extern`으로 참조하기 때문이다.

그다음 실제로 실행되는 `HAL_UART_MspInit()`의 USART1 분기에 아래 순서로 넣는다.

```c
__HAL_RCC_DMA1_CLK_ENABLE();

hdma_usart1_tx.Instance = DMA1_Channel4;
hdma_usart1_tx.Init.Request = DMA_REQUEST_2;
hdma_usart1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
hdma_usart1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
hdma_usart1_tx.Init.MemInc = DMA_MINC_ENABLE;
hdma_usart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
hdma_usart1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
hdma_usart1_tx.Init.Mode = DMA_NORMAL;
hdma_usart1_tx.Init.Priority = DMA_PRIORITY_LOW;

if (HAL_DMA_Init(&hdma_usart1_tx) != HAL_OK)
{
  Error_Handler();
}

__HAL_LINKDMA(uartHandle, hdmatx, hdma_usart1_tx);
```

각 단계의 의미는 다음과 같다.

1. DMA1 clock을 켜지 않으면 DMA 레지스터 설정 자체가 동작하지 않는다.
2. `HAL_DMA_Init()`이 channel/request/direction/mode를 DMA handle과 하드웨어에 적용한다.
3. `__HAL_LINKDMA()`가 `huart1.hdmatx`와 `hdma_usart1_tx.Parent`를 서로 연결한다.
4. 이 link가 없으면 `HAL_UART_Transmit_DMA()`와 HAL callback 체인이 올바른 DMA handle을 찾지 못한다.

NVIC도 같은 활성 경로에서 설정한다.

```c
HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 6, 0);
HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);

HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
HAL_NVIC_EnableIRQ(USART1_IRQn);
```

DeInit에는 반대 작업을 넣는다.

```c
HAL_NVIC_DisableIRQ(DMA1_Channel4_IRQn);
HAL_NVIC_ClearPendingIRQ(DMA1_Channel4_IRQn);
HAL_DMA_DeInit(uartHandle->hdmatx);
```

### 4. 활성 IRQ 파일을 HAL에 연결한다

`src/bsp/stm32l4xx_it.c` 상단에 handle을 선언한다.

```c
extern DMA_HandleTypeDef hdma_usart1_tx;
extern UART_HandleTypeDef huart1;
```

같은 파일의 IRQ handler는 다음처럼 HAL handler를 호출해야 한다.

```c
void DMA1_Channel4_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_usart1_tx);
}

void USART1_IRQHandler(void)
{
  HAL_UART_IRQHandler(&huart1);
}
```

`src/bsp/stm32l4xx_it.h`에도 두 함수의 prototype이 있어야 한다.

DMA IRQ만 있으면 충분해 보이지만 UART TX에서는 그렇지 않다. DMA IRQ는 마지막 byte를 USART에 넘긴 시점을 처리하고, HAL은 실제 마지막 stop bit까지 끝났는지 확인하기 위해 USART의 TC interrupt를 사용한다. 최종 `HAL_UART_TxCpltCallback()`은 USART1 IRQ까지 실행된 뒤 호출된다.

### 5. DMA에 넘길 버퍼의 수명을 보장한다

아래 코드는 잘못된 예다.

```c
void send(void)
{
  uint8_t packet[50];
  HAL_UART_Transmit_DMA(&huart1, packet, sizeof(packet));
}
```

`send()`가 끝나도 DMA는 계속 `packet` 주소를 읽는다. 하지만 `packet`은 stack 지역변수라 다른 함수가 같은 메모리를 덮어쓸 수 있다.

현재 `uart.c`는 이 문제를 피하려고 다음 정적 버퍼와 상태를 사용한다.

```c
static uint8_t           tx_buf[2][256];
static volatile bool     tx_busy;
static volatile bool     tx_pending;
static volatile uint8_t  tx_active_buf;
static volatile uint16_t tx_pending_length;
```

전송 순서는 다음과 같다.

1. 첫 패킷을 `tx_buf[tx_active_buf]`에 복사한다.
2. `HAL_UART_Transmit_DMA()`로 그 정적 버퍼를 시작한다.
3. 전송 중 새 표시 패킷이 오면 반대쪽 버퍼에 복사한다.
4. 또 새 패킷이 오면 아직 전송하지 않은 반대쪽 버퍼를 다시 덮어써서 가장 최신 snapshot만 남긴다.
5. TX 완료 callback에서 pending 버퍼를 즉시 이어서 보낸다.

이 정책은 모든 표시 패킷이 전체 화면 상태를 담는 경우에만 적합하다. 명령, ACK, 이벤트처럼 한 개도 잃으면 안 되는 패킷은 별도 queue 또는 보장 송신 API가 필요하다.

### 6. DMA를 실제로 시작한다

CubeMX 설정, MSP, IRQ를 모두 넣어도 아래 시작 API를 호출하지 않으면 DMA는 한 번도 동작하지 않는다.

```c
HAL_StatusTypeDef status;

status = HAL_UART_Transmit_DMA(&huart1, tx_buf[buf_index], length);
if (status != HAL_OK)
{
  /* busy/error 상태 복구 또는 다음 주기 재시도 */
}
```

현재 프로젝트에서는 `uartWrite(_DEF_UART2, ...)`가 `uartDisplayWriteDma()`를 호출하면서 이 작업을 수행한다. 반대로 `_DEF_UART1` CLI는 기존 `HAL_UART_Transmit(..., 100)` blocking 방식을 유지한다.

### 7. 완료 callback에서 software 상태를 정리한다

`HAL_UART_TxCpltCallback()`에서 다음 중 하나를 해야 한다.

- pending 패킷이 없으면 `tx_busy = false`
- pending 패킷이 있으면 버퍼 index를 바꾸고 다음 `HAL_UART_Transmit_DMA()` 호출

main과 callback이 같은 상태 변수를 수정하므로 `tx_busy`, `tx_pending`, index와 length를 함께 바꾸는 구간은 PRIMASK 임계영역으로 보호한다. `volatile`만 붙이는 것으로 여러 변수의 상태 전환이 원자적으로 되지는 않는다.

### 8. 실제로 동작하는지 단계별로 확인한다

1. `uartOpen(_DEF_UART2, 115200)`가 `true`를 반환하는지 본다.
2. debugger에서 `huart1.hdmatx == &hdma_usart1_tx`인지 확인한다.
3. `HAL_UART_Transmit_DMA()` 반환값이 `HAL_OK`인지 확인한다.
4. `DMA1_Channel4_IRQHandler()`에 breakpoint 또는 counter를 둔다.
5. `USART1_IRQHandler()`에도 counter를 둔다.
6. `HAL_UART_TxCpltCallback()`이 마지막에 호출되는지 확인한다.
7. logic analyzer로 PA9에서 115200-8-N-1 신호가 나오는지 확인한다.
8. 10 ms마다 50 byte를 연속 호출해 main loop가 더 이상 약 4.3 ms blocking되지 않는지 측정한다.
9. 전송 중 여러 번 호출해 최종 패킷이 가장 최신 snapshot인지 확인한다.
10. UART 재초기화와 line error 뒤 `tx_busy`가 영구적으로 true에 남지 않는지 확인한다.

## DMA 하나를 완성하는 5단계

1. CubeMX에서 DMA request/channel과 동작 방식을 정한다.
2. 활성 드라이버에서 DMA clock, handle 초기화, peripheral handle 연결을 한다.
3. NVIC를 설정하고 활성 IRQ handler에서 `HAL_DMA_IRQHandler()`를 호출한다.
4. 수명이 보장된 버퍼로 `HAL_*_DMA()` 시작 API를 호출한다.
5. 완료·오류 callback에서 상태 정리, 재시작 또는 다음 전송을 처리한다.

하나라도 빠지면 DMA는 시작되지 않거나, 첫 전송 뒤 멈추거나, callback이 오지 않는다.

## 1. CubeMX에서 결정할 것

- 기존 DMA channel/request와 충돌하지 않는지 확인한다.
- TX는 보통 `MEMORY_TO_PERIPH`, RX/ADC는 `PERIPH_TO_MEMORY`다.
- Peripheral increment는 보통 Disable, Memory increment는 버퍼 전송이면 Enable이다.
- Peripheral/Memory data alignment는 peripheral 데이터 폭과 버퍼 원소 타입에 맞춘다.
- 일회성 전송은 Normal, 계속 반복할 ADC/stream은 Circular 여부를 설계한 뒤 선택한다.
- DMA priority와 NVIC priority를 각각 정한다. 둘은 다른 설정이다.
  - `DMA_PRIORITY_*`: DMA 채널끼리의 버스 중재 우선순위
  - `HAL_NVIC_SetPriority()`: CPU 인터럽트 선점 우선순위
- DMA channel IRQ를 NVIC에서 켠다.
- UART Receive-to-Idle이나 UART TX 완료처럼 peripheral IRQ도 필요한 경우 UART global IRQ도 켠다.

현재 주요 할당은 다음과 같다.

- ADC1: DMA1 Channel 1, Circular
- USART1 TX 표시 송신: DMA1 Channel 4 / Request 2, Normal
- USART2 RX: DMA1 Channel 6 / Request 2로 설정만 보존, 현재 미사용

## 2. Generate Code 뒤 확인할 참고 파일

- `.ioc`: channel/request/direction/mode/NVIC 값
- `src/lib/cube_l432/Core/Src/<peripheral>.c`: DMA handle, `HAL_DMA_Init()`, `__HAL_LINKDMA()`
- `src/lib/cube_l432/Core/Src/dma.c`: DMA clock과 NVIC 설정
- `src/lib/cube_l432/Core/Src/stm32l4xx_it.c/.h`: extern handle, IRQ handler와 prototype
- `src/lib/cube_l432/Core/Src/main.c`: CubeMX가 가정한 초기화 순서

생성된 Core와 현재 custom source를 동시에 빌드하면 `huart`, `hdma`, MSP, callback, IRQ handler가 중복 정의될 수 있으므로 Core 전체를 빌드에 추가하지 않는다.

## 3. 실제 빌드되는 활성 소스에 옮길 것

드라이버는 `src/hw/driver`, IRQ는 `src/bsp/stm32l4xx_it.c/.h`가 활성 경로다.

### 드라이버 또는 MSP

- IRQ에서 참조할 `DMA_HandleTypeDef`는 `static`이 아닌 전역 handle로 선언한다.
- 실제 실행되는 초기화 경로에서 `__HAL_RCC_DMA1_CLK_ENABLE()`을 호출한다.
- CubeMX 값과 동일하게 아래 필드를 채우고 `HAL_DMA_Init()`을 호출한다.
  - `Instance`, `Request`, `Direction`
  - `PeriphInc`, `MemInc`
  - `PeriphDataAlignment`, `MemDataAlignment`
  - `Mode`, `Priority`
- peripheral handle에 반드시 연결한다.
  - UART TX: `__HAL_LINKDMA(huart, hdmatx, hdma_tx)`
  - UART RX: `__HAL_LINKDMA(huart, hdmarx, hdma_rx)`
  - ADC: `__HAL_LINKDMA(hadc, DMA_Handle, hdma_adc)`
- NVIC priority를 설정하고 IRQ를 enable한다.
- DeInit에는 IRQ disable/clear-pending과 `HAL_DMA_DeInit()`을 넣는다.

### 활성 IRQ 파일

- `src/bsp/stm32l4xx_it.c`에 DMA handle `extern`을 선언한다.
- startup vector와 정확히 같은 이름의 handler에서 HAL handler를 호출한다.

```c
void DMA1_Channel4_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_usart1_tx);
}
```

- `src/bsp/stm32l4xx_it.h`에도 handler prototype을 추가한다.

## 4. 애플리케이션에서 직접 구현할 것

CubeMX는 DMA를 사용할 준비까지만 한다. 실제 동작은 아래 API를 호출해야 시작된다.

- UART TX: `HAL_UART_Transmit_DMA()`
- UART RX: `HAL_UART_Receive_DMA()` 또는 `HAL_UARTEx_ReceiveToIdle_DMA()`
- ADC: `HAL_ADC_Start_DMA()`

추가로 다음 정책을 코드로 정해야 한다.

- `HAL_BUSY`/`HAL_ERROR`일 때 재시도, 폐기, 최신값 덮어쓰기 또는 queue 중 무엇을 할지
- Normal mode 완료 뒤 다시 시작할지
- Circular mode에서 DMA write 위치와 CPU read 위치를 어떻게 관리할지
- half/full/idle/complete/error callback에서 무엇을 할지
- peripheral 재초기화 때 진행 중 DMA와 software busy 상태를 어떻게 초기화할지

## 5. 버퍼 수명과 동시성

- DMA가 끝날 때까지 버퍼 주소가 유효하고 내용이 변하지 않아야 한다.
- 함수 지역 배열이나 `uartPrintf()`의 stack buffer를 DMA에 직접 넘기지 않는다.
- TX는 static/global 버퍼로 복사하거나, 호출자가 완료까지 소유권을 유지하게 한다.
- RX/ADC DMA 버퍼도 static/global로 둔다.
- main과 callback이 함께 쓰는 busy/index/count는 임계영역 또는 atomic 정책으로 보호한다.
- `volatile`은 값 재읽기를 보장할 뿐 여러 상태를 함께 바꾸는 연산을 원자적으로 만들지는 않는다.

## 현재 USART1 표시 TX 흐름

1. `uartWrite(_DEF_UART2, ...)`가 내부 static double buffer에 복사한다.
2. `HAL_UART_Transmit_DMA()`가 USART1 TX DMA를 시작한다.
3. DMA1 Channel 4 IRQ가 `HAL_DMA_IRQHandler(&hdma_usart1_tx)`를 호출한다.
4. HAL이 마지막 UART TC 확인을 위해 USART1 IRQ를 사용한다.
5. USART1 IRQ가 `HAL_UART_IRQHandler(&huart1)`를 호출한다.
6. `HAL_UART_TxCpltCallback()`이 busy를 해제하거나 최신 pending packet을 이어서 보낸다.

따라서 USART1 표시 TX는 DMA IRQ와 USART1 IRQ가 모두 필요하다.

현재 USART1은 `UART_MODE_TX`이며 PA9와 TX DMA만 사용한다. USART2 RX DMA handle은 CubeMX 설정을 보존한 준비 코드이고, 실제 CLI RX는 `HAL_UART_Receive_IT()` 1바이트 재등록 방식이다. 이후 다른 UART RX를 DMA로 바꾸려면 시작 API, persistent RX buffer, Normal 재등록 또는 Circular/Idle 소비 정책까지 함께 구현해야 한다.

## 추가 후 검증

- clean build와 link가 성공하고 duplicate symbol이 없는지 확인한다.
- DMA 시작 API 반환값이 `HAL_OK`인지 확인한다.
- debugger에서 peripheral handle의 DMA link가 NULL이 아닌지 확인한다.
- DMA IRQ, peripheral IRQ, 완료 callback에 counter를 두어 순서대로 실행되는지 확인한다.
- logic analyzer로 API 호출 시점, wire 전송, callback 시점을 확인한다.
- 전송 시간보다 빠르게 연속 호출해 busy/queue/latest-wins 정책을 검증한다.
- UART line error, ADC overrun, peripheral 재초기화 뒤 busy 상태가 영구 고착되지 않는지 확인한다.
