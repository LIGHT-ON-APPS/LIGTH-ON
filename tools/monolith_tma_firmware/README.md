# Monolith TMA-1 TEM 45채널 스냅샷 2 Hz 패치

> 이 폴더에는 패치가 두 개 있다. 이 문서는 `monolith-v2.3.0-tem-snapshot-2hz.patch`를
> 다룬다. CAN 진단 강화 패치는 `README-can-diagnostics.md`를 본다. 둘 다 `can.c`를
> 건드리지만 hunk가 겹치지 않아 순서에 상관없이 함께 적용된다.

## 목적

Monolith TMA-1이 Orion Thermistor Expansion Module(TEM)의 45개 센서를 같은 측정
순환으로 묶어 서버에 전달하되, live 전송률은 최대 2 Hz로 유지한다.

기준 소스는 Monolith `v2.3.0`, 커밋
`d9067e9f2e4a6007b0157af1ced1846690d077b0`이다. 공식 저장소의 해당 커밋을 직접
확인하고 패치 적용 및 호스트 테스트를 검증했다.

## 동작

- 대상은 `extended=true`, `remote=false`, DLC 8,
  CAN ID `0x1838F380`뿐이다.
- 센서 ID는 `data[3] & 0x7f`이며, `0 → 44`가 빠짐없이 순서대로 들어온 경우에만
  하나의 스냅샷으로 확정한다.
- 중간 ID가 빠지거나 순서가 어긋나면 그 수집분은 폐기하고 다음 센서 0부터 다시
  수집한다. 따라서 실제 활성 센서가 0~44가 아니거나 sparse하면 TEM live
  스냅샷은 생성되지 않으며 SD 원본에만 남는다.
- 3개 정적 버퍼를 사용한다.
  1. 현재 서버로 보내는 immutable transmit 스냅샷
  2. 가장 최근에 완성된 immutable pending 스냅샷
  3. 다음 0~44 순환을 받는 mutable capture 버퍼
- 전송 중 여러 순환이 완성되면 오래된 pending을 최신 complete 스냅샷으로 교체한다.
  다음 전송이 약 45초 전 스냅샷으로 밀리지 않게 하는 정책이다.
- frozen `log_t` 전체를 복사하므로 서버로 늦게 보내도 각 센서의
  `timestamp`와 checksum은 실제 CAN 수신 시점 그대로다.
- 한 레코드씩 센서 0~44 순서로 보낸다. 전송 간격은 기존
  `storage.device.intv`를 따르되 500 ms보다 작아질 수 없으므로 최대 2 Hz다.
  기본 500 ms 설정에서 첫 프레임부터 마지막 프레임까지 22.0초, 다음 스냅샷의
  첫 프레임까지 포함한 처리 주기는 22.5초다.
- 정상 연속 수집 시 서버가 마지막 센서를 받을 때 스냅샷 마지막 측정값은 대략
  22~27초 전, 첫 측정값은 추가 4.4초 전이다. 즉 센서별 최대 지연은 대략
  31초로 봐야 한다.

## SD 로깅 보존

기존 `LOG(LOG_TYPE_CAN, &log)` 호출의 위치와 횟수는 유지한다. 스냅샷에는 SD queue가
정상적으로 받은 canonical `log_t`만 넣는다.

파일 다운로드 등으로 `logqueue`가 없거나 가득 차 `LOG()`가 실패하면 현재 만들던
capture만 무효화하고 다음 완전한 0~44 순환부터 다시 만든다. 이미 frozen된
transmit/pending은 손상하지 않는다. `canlogqueue` 삭제 전에 queue에 들어갔지만 아직
MQTT로 publish되지 않은 prefix가 사라질 수 있으므로, queue가 없는 동안 transmit
인덱스를 0으로 되돌리고 복구 후 frozen 스냅샷 전체를 다시 보낸다. 파일 작업 동안
live 전송은 늦어질 수 있지만 원래 device timestamp는 바뀌지 않는다.

이 패치는 차량 CAN에 프레임을 추가 송신하지 않는다. TEM 수신 프레임의 SD 기록도
줄이지 않는다. 정적 RAM 증가는 `3 × 45 × sizeof(log_t)` = 3,240 bytes와 작은
상태 구조체(현재 16 bytes), 합계 약 3,256 bytes다.

## 서버 전달 보장 범위

`canlogqueue`가 가득 차면 같은 센서를 다음 loop에서 재시도하며, queue가 받아야 다음
센서로 진행한다. 다만 queue 수락은 서버 저장 완료를 뜻하지 않는다. 기존 Monolith는
MQTT QoS 0으로 publish하고 publish 결과를 확인하지 않으므로 Wi-Fi 단절이나 MQTT 유실
시 서버에는 partial 스냅샷이 남을 수 있다. 재연결 시 queue의 레코드가 몰아서 전달될
수도 있다. Downloader의 `complete`와 측정 span을 계속 확인해야 하며 안전 제어에는
이 경로를 사용하지 않는다.

## 패치 적용

```powershell
git -C <monolith-source> checkout d9067e9f2e4a6007b0157af1ced1846690d077b0
git -C <monolith-source> apply --check C:\project\LIGHT_ON\tools\monolith_tma_firmware\monolith-v2.3.0-tem-snapshot-2hz.patch
git -C <monolith-source> apply C:\project\LIGHT_ON\tools\monolith_tma_firmware\monolith-v2.3.0-tem-snapshot-2hz.patch
```

기존 10 Hz 우회 패치와 동시에 적용하지 않는다. 이 패치가 그 설계를 대체한다.

## 호스트 정책 테스트

패치에는 ESP-IDF 없이 실행 가능한 상태 머신 테스트가 포함된다.

```powershell
gcc -std=c11 -Wall -Wextra -Werror `
  <monolith-source>\device\firmware\test\test_can_tem_snapshot.c `
  -o <monolith-source>\device\firmware\test\test_can_tem_snapshot.exe

& <monolith-source>\device\firmware\test\test_can_tem_snapshot.exe
```

테스트 범위:

- 정확한 CAN ID/extended/RTR/DLC 정책과 500 ms 최소 간격
- 부팅이 순환 중간에 일어난 경우 다음 sensor 0까지 대기
- missing, gap, duplicate 0, 범위 밖 ID 및 fault bit
- 3버퍼의 transmit 불변성과 최신 pending 교체
- queue 실패 시 같은 sensor 재시도
- SD queue 실패 시 capture만 무효화
- live queue 삭제 후 immutable snapshot을 sensor 0부터 재생
- FreeRTOS tick wrap 이후에도 500 ms 간격 유지
- 센서별 원래 측정 timestamp 보존

## 하드웨어 적용 전 검증

이 저장소는 검토 가능한 패치만 제공한다. TMA 플래시나 차량 배포를 자동 수행하지
않는다. ESP-IDF full build 후 벤치에서 다음을 확인한다.

1. 패치 전후 SD CAN 프레임 수와 timestamp가 동일한지 확인한다.
2. CAN 원본에서 센서 ID가 실제로 연속 `0..44`인지 확인한다.
3. 안정 연결되고 backlog가 없을 때 서버의 TEM 수신 평균이 2 Hz 이하인지 확인한다.
4. 한 complete 행의 `measurement_span_ms`가 약 4,400 ms인지 확인한다.
5. 안정 연결되고 backlog가 없을 때 서버 delivery duration이 약 22초인지,
   Downloader가 `complete=true`인지 확인한다.
6. Wi-Fi 단절/재연결 및 파일 다운로드 후, 이미 전달된 prefix가 있으면 partial 뒤
   sensor 0부터 full replay가 남고 prefix가 없으면 full만 남는지 확인한다.
7. `0x1839F380` 및 다른 CAN ID의 기존 live 주기가 변하지 않았는지 확인한다.
