# Monolith TMA-1 CAN 진단 패치

## 목적

TMA가 CAN 프레임을 한 건도 받지 못하는 상태가 2026-07-30부터 이어지고 있는데,
로그로는 **RX 단선인지 비트레이트 불일치인지 구분할 수 없다.** 이 패치는 그 구분에
필요한 컨트롤러 카운터를 내보내고, 그동안 진단을 왜곡한 결함 두 개를 고친다.
bus-off에 빠지면 재부팅 전까지 조용히 멈추는 문제도 함께 고친다.

`monolith-v2.3.0-can-diagnostics.patch`

## 기준 소스

장치가 보고하는 실제 펌웨어는 `lighton/d/ver` 기준 **v2.3.0 (`d9067e9f`)**이다.
패치는 운영 VM의 `/opt/monolith`(태그 `v2.3.1`, `5fbcdf3`) 트리에서 만들었고, 같은
트리에서 기존 `monolith-v2.3.0-tem-snapshot-2hz.patch`가 그대로 적용되는 것을 확인해
두 버전의 해당 영역이 동일함을 확인했다. 두 태그 어느 쪽에도 적용된다.

## 변경 내용

### 1. bus-off 자동 복구 (`main/peripheral/can.c`)

ESP-IDF TWAI는 bus-off에서 **스스로 빠져나오지 않는다.** 기존 코드는 alert를 로그로만
남기고 `twai_initiate_recovery()`를 부르지 않아서, 한 번 bus-off에 들어가면 다음
재부팅까지 송수신이 모두 죽는다. 지금까지 관측된 alert는 에러 패시브(`0x1000`)까지지만,
배선이 복구돼 트래픽이 늘면 도달할 수 있는 상태다.

`TWAI_ALERT_BUS_OFF`에서 복구를 시작하고, `TWAI_ALERT_BUS_RECOVERED`에서 `twai_start()`로
다시 올린다. 후자를 받으려면 alert 마스크에 추가해야 해서 `CAN_ALERT_ENABLED`도 넓혔다.

### 2. 컨트롤러 카운터 보고 (`main/peripheral/can.c`)

`twai_get_status_info()`를 1초마다 읽어 **값이 바뀐 항목만** syslog로 올린다. 정상
주행 중에는 아무것도 남기지 않는다.

### 3. alert 반복 가시화 (`main/peripheral/can.c`)

기존 조건 `if (alerts != prev_alerts)`는 같은 alert가 반복되면 로그를 완전히 없앤다.
SD 로그 5,721,855 레코드에 `CANALT`가 **단 1건**이었던 건 에러가 드물어서가 아니라
이것 때문이다. 새 조합이면 즉시 남기고, 같은 조합이 이어지면 5초마다 그동안의
반복 횟수를 `CANRPT`로 남긴다.

### 4. `cmd/can`의 ID 파싱 (`main/network/mqtt.c`)

`atol(dir[3])`은 **10진 파싱**이라 `lighton/cmd/can/7FF`가 ID `0x7FF`가 아니라 **ID 7**로
나갔다. 2026-08-18 벤치 테스트가 이 상태로 진행됐고 로그의 `CANTX:7`이 그 흔적이다.
`dir[3]`이 없는 `lighton/cmd/can`은 `atol(NULL)`로 **장치를 죽였다.**

`can_id_from_token()`으로 분리해 16진으로 읽고, NULL·빈 문자열·잉여 문자·29비트 초과를
모두 거부한다. 기존의 "0은 invalid" 동작과 `> 0x7FF`면 확장 프레임으로 승격하는 동작은
그대로다.

### 5. NVS `can_en` 키 오염 (`main/main.c`)

CAN 기본값 3개를 전부 `can_en` 키에 쓰고 있었다(`can_bps`/`can_filter`/`can_mask`에
써야 한다). 런타임 값은 매 부팅 기본값으로 올바르게 채워지므로 **동작에는 영향이
없었고**, 실제로 `d/cfg` 덤프에서 `bps=500K`, `filter=0`, `mask=0xFFFFFFFF`를 확인했다.
다만 이 세 키가 NVS에 영영 저장되지 않고, `can_en`이 u8과 u32로 번갈아 덮여 매 부팅
타입 불일치를 일으킨다. 오타 교정만 했다.

기존 NVS에 남은 u32 `can_en`은 첫 부팅에서 타입 불일치로 읽기에 실패하고 곧바로 u8
`true`로 다시 써지므로, 별도 정리 없이 자동 복구된다.

## 새 syslog 태그

`system_event_t.msg`가 16바이트라 태그는 15자를 넘을 수 없다. 최악값에서도 잘리지
않는 것을 호스트 테스트로 검증했다.

| 태그 | 의미 |
|---|---|
| `CANALT:<hex>` | alert 조합이 바뀌었다. `0x100` 경고한계, `0x200` 버스에러, `0x1000` 에러패시브, `0x2000` bus-off |
| `CANRPT:<hex>` | 직전 `CANALT`가 5초 동안 반복된 횟수 |
| `CANEC:<TEC>/<REC>/<state>` | 송신·수신 에러 카운터(2자리 hex)와 상태. state `0` 정지, `1` 동작, `2` bus-off, `3` 복구중 |
| `CANBE:<hex>` | `bus_error_count` 누적 |
| `CANMS:<hex>` | `rx_missed_count` 누적 |
| `CANOV:<hex>` | `rx_overrun_count` 누적 |
| `CANBOFF:REC` / `:ERR` | bus-off 복구 시작 / 시작 실패 |
| `CANBOFF:UP` / `:DOWN` | 복구 후 재시작 성공 / 실패 |

## 읽는 법

이 패치의 값어치는 이 표에 있다. 지금까지는 "프레임이 안 온다"는 사실 하나뿐이라
아래를 구분할 수 없었다.

| 관측 | 해석 |
|---|---|
| `CANEC`의 TEC만 오르고 REC와 `CANBE`는 0 | 우리 송신이 ACK를 못 받는다. **RX 단선**이거나 버스에 응답할 노드가 없다 |
| REC와 `CANBE`가 함께 오른다 | 엣지는 보는데 디코드에 실패한다. **비트레이트 불일치** 또는 CAN_H/L 반전 |
| TEC·REC·`CANBE` 모두 0인데 수신도 0 | 신호가 아예 들어오지 않는다. **완전 단선**(2026-08-14의 상태) |
| `CANMS`/`CANOV`가 오른다 | 수신은 되는데 태스크가 큐를 못 비운다. 배선 문제가 아니다 |

서버에서는 이렇게 본다.

```bash
docker exec -i monolith-v2-postgres-1 psql -U monolith -d monolith -c "select message, count(*) as n, min(received_at) as first, max(received_at) as last from events where message like 'CAN%' group by message order by n desc limit 20;"
```

## 적용

```powershell
git -C <monolith-source> apply --check C:\project\LIGHT_ON\tools\monolith_tma_firmware\monolith-v2.3.0-can-diagnostics.patch
git -C <monolith-source> apply C:\project\LIGHT_ON\tools\monolith_tma_firmware\monolith-v2.3.0-can-diagnostics.patch
```

**TEM 스냅샷 패치와 함께 적용해도 된다.** 둘 다 `can.c`를 건드리지만 겹치는 hunk가
없도록 진단 상태를 파일 스코프에 뒀다. 어느 순서로 적용해도 `git apply`가 통과하는
것을 양방향으로 확인했다.

## 호스트 테스트

ESP-IDF 없이 돌아간다.

```bash
gcc -std=c11 -Wall -Wextra -Werror <monolith-source>/device/firmware/test/test_can_diag.c -o test_can_diag
```

검증 범위는 모든 진단 태그가 최악값에서도 16바이트에 들어가는지, 그리고 ID 파서가
`7FF`를 `0x7FF`로 읽고 NULL·빈값·`12G`·29비트 초과·`0`을 거부하는지다. 21개 항목이
`-Werror`로 통과한다.

## 적용 전 알아둘 것

- **플래시에는 보드 물리 접근이 필요하다.** Monolith에 OTA는 없다. 이 저장소는 검토
  가능한 패치만 제공하며 플래시를 자동 수행하지 않는다.
- 이 패치는 **원인을 고치지 않는다.** 지금까지의 감사에서 설정·필터·GPIO·비트레이트는
  모두 정상으로 확인됐고, 남은 것은 물리 계층이다. 이 패치는 그 물리 계층 문제가
  어떤 종류인지 로그만 보고 판정할 수 있게 만든다.
- 에러가 계속 요동치면 `CANEC`가 최대 초당 1건 생긴다. 하루 최대 약 86,400건으로,
  기존 `ADC_READ_FAIL`(분당 640건)보다는 훨씬 적지만 정상 복구 후에도 남아 있다면
  `CAN_STATUS_POLL_MS`를 늘린다.
- ESP-IDF full build 후 벤치에서 패치 전후의 SD CAN 레코드 수와 timestamp가 같은지
  먼저 확인한다. 이 패치는 SD 기록 경로를 건드리지 않는다.
