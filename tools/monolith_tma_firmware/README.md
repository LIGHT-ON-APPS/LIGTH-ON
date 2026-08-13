# Monolith TMA-1 TEM 10 Hz 전송 패치

## 목적

Monolith TMA-1이 수신한 Orion Thermistor Expansion Module(TEM)의 개별 센서 프레임을
서버에 실시간으로 전달할 때, `0x1838F380` 확장 CAN 프레임만 기존 장치 전송 주기
(`storage.device.intv`, 현재 500 ms)의 제한을 우회한다.

기준 소스는 Monolith `v2.3.0`, 커밋
`d9067e9f2e4a6007b0157af1ced1846690d077b0`이다. 현재 차량 TMA에서 보고된 펌웨어와
동일한 기준이다.

## 변경 범위

- `LOG(LOG_TYPE_CAN, &log)` 호출은 수정하지 않는다. 따라서 SD 로깅 경로와 호출 순서는
  기존과 동일하다.
- `extended == true && remote == false && DLC == 8 && identifier == 0x1838F380`일
  때만 live 전송 간격을 우회한다.
- 표준 프레임의 같은 숫자 ID, TEM 요약 프레임 `0x1839F380`, 기타 모든 CAN ID는 기존
  500 ms 제한을 그대로 적용한다.
- 기존과 마찬가지로 live queue가 없거나 가득 차면 전송을 보장하지 않는다. 이 패치는
  의도적인 500 ms 샘플링 제한만 제거하며, MQTT QoS 0이나 Wi-Fi 단절로 인한 유실까지
  해결하지 않는다.
- 차량 CAN 버스에 메시지를 추가로 송신하지 않는다. 수신한 프레임의 서버 복사 빈도만
  변경한다.

이 구현은 target을 10 Hz로 다시 제한하는 것이 아니라 유효한 target의 interval
검사를 완전히 우회한다. 같은 extended ID가 비정상적으로 더 빠르게 들어오면 모두
live queue 입력을 시도할 수 있으므로 배포 전 ID 충돌과 burst 부하를 벤치에서
확인한다. RTR 또는 DLC 8이 아닌 프레임은 우회하지 않고 기존 500 ms 정책을 따른다.

`0x1838F380`의 마지막 바이트 `0x80`은 현재 TEM의 J1939 source address가 `0x80`이라는
가정이다. TEM 주소가 바뀌거나 모듈이 추가되면 대상 ID 정책도 함께 검토해야 한다.

## 패치 적용

Monolith 공식 소스를 별도 위치에 준비한 후 다음과 같이 적용한다.

```powershell
git -C <monolith-source> checkout d9067e9f2e4a6007b0157af1ced1846690d077b0
git -C <monolith-source> apply --check C:\project\LIGHT_ON\tools\monolith_tma_firmware\monolith-v2.3.0-tem-live-10hz.patch
git -C <monolith-source> apply C:\project\LIGHT_ON\tools\monolith_tma_firmware\monolith-v2.3.0-tem-live-10hz.patch
```

## 정책 단위 테스트

패치에는 ESP-IDF와 무관하게 호스트 C 컴파일러로 실행할 수 있는 테스트를 포함한다.

```powershell
gcc -std=c11 -Wall -Wextra -Werror `
  <monolith-source>\device\firmware\test\test_can_live_policy.c `
  -o <monolith-source>\device\firmware\test\test_can_live_policy.exe

& <monolith-source>\device\firmware\test\test_can_live_policy.exe
```

테스트는 다음 경계를 확인한다.

1. 확장 `0x1838F380`은 경과 시간이 0이어도 전송한다.
2. 표준 `0x1838F380`은 우회하지 않는다.
3. RTR, DLC 8이 아닌 frame, 확장 `0x1839F380` 및 다른 source address는 우회하지 않는다.
4. 다른 프레임은 499 ms에서 차단되고 500 ms부터 전송된다.

## 펌웨어 빌드 및 차량 적용 전 검증

ESP-IDF 환경에서 `device/firmware`의 공식 빌드를 수행한 뒤, 차량에 바로 적용하지 말고
벤치에서 다음을 확인한다.

1. SD 로그의 CAN 프레임 수가 패치 전후 동일한지 확인한다.
2. 서버에서 확장 `0x1838F380`이 약 10 Hz로 수신되는지 확인한다.
3. `0x1839F380`과 일반 CAN ID의 live 주기가 기존과 동일한지 확인한다.
4. 10~15초 수집했을 때 실제 활성 센서 ID 집합을 기록한다. 45개가 연속
   `0~44`가 아니면 Downloader mapping을 차량 설정에 맞춰 변경한다.
5. Wi-Fi 단절 후에도 SD 로깅이 계속되는지 확인한다.

이 디렉터리는 검토·재현 가능한 소스 패치만 제공한다. TMA 하드웨어 플래시나 서버 배포는
자동으로 수행하지 않는다.
