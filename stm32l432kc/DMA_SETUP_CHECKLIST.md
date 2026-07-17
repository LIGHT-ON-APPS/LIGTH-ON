# STM32L432 DMA 추가 체크리스트

이 프로젝트에서 CubeMX의 `Generate Code`는 DMA를 실제 펌웨어에 자동 적용하지 않는다.
`src/lib/cube_l432/Core`가 빌드에서 제외되어 있으므로, 생성된 코드는 설정 참고 원본으로 사용하고 필요한 부분을 활성 소스로 옮겨야 한다.

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
- USART1 RX: DMA1 Channel 5 / Request 2로 설정만 보존, 현재 미사용
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

현재 USART1/USART2 RX DMA handle은 CubeMX 설정을 보존한 준비 코드일 뿐이다. 실제 RX는 양쪽 모두 `HAL_UART_Receive_IT()` 1바이트 재등록 방식이다. RX를 DMA로 바꾸려면 시작 API, persistent RX buffer, Normal 재등록 또는 Circular/Idle 소비 정책까지 함께 구현해야 한다.

## 추가 후 검증

- clean build와 link가 성공하고 duplicate symbol이 없는지 확인한다.
- DMA 시작 API 반환값이 `HAL_OK`인지 확인한다.
- debugger에서 peripheral handle의 DMA link가 NULL이 아닌지 확인한다.
- DMA IRQ, peripheral IRQ, 완료 callback에 counter를 두어 순서대로 실행되는지 확인한다.
- logic analyzer로 API 호출 시점, wire 전송, callback 시점을 확인한다.
- 전송 시간보다 빠르게 연속 호출해 busy/queue/latest-wins 정책을 검증한다.
- UART line error, ADC overrun, peripheral 재초기화 뒤 busy 상태가 영구 고착되지 않는지 확인한다.
