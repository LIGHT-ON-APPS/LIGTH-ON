# Monolith collector Orion TEM patch

이 폴더는 운영 서버의 Monolith collector에 Orion Thermistor Expansion
Module(TEM) 개별 온도 프레임을 추가하기 위한 **검토·배포 패키지**다.
DB migration과 collector는 2026-08-14 운영 VM에 적용했다. 당시 TMA가 연결되어
있지 않아 실제 차량 CAN 원본 약 10 Hz와 TMA frozen snapshot 최대 2 Hz 전달에서
45채널이 모두 저장되는지는 아직 검증하지 못했다. 서버 decoder에는 별도 10 Hz
요구가 없으며 들어온 유효 프레임을 그대로 저장한다. 상세 백업·image·검증 기록은
`TEM_45CH_DATA_PIPELINE_CHANGE.md`를 따른다.

## 범위와 전제

- 대상은 29-bit extended CAN ID `0x1838F380`, DLC 8뿐이다.
- 현재 차량은 TEM Expansion Module #1, J1939 source address `0x80`으로
  설정됐다고 가정한다. Module #2라면 ID 끝이 `0x81`이 되므로 현재 strict
  matcher에는 저장되지 않는다. 실제 설정 변경 시 허용 ID 목록을 명시적으로
  추가해야 한다.
- `data[2]`는 signed int8 온도(`1 °C/bit`)다.
- `data[3] & 0x7f`는 module-local zero-based 센서 ID다. TEM의 유효 범위
  `0..79`는 wire 값 그대로 DB에 저장한다. disabled 채널을 건너뛰면 활성 센서가
  45개여도 ID가 `0..44`로 연속하지 않을 수 있기 때문이다.
- 현재 Downloader의 45열 mapping은 ID `0..44`를 사용자 표기 `1..45`로
  변환한다. 배포 전 실제 활성 ID 집합이 정확히 `0..44`인지 확인한다. sparse하면
  strict TMA snapshot이 live로 나오지 않으므로 TEM 설정을 연속 ID로 바꾸거나,
  TMA 상태 머신과 Downloader mapping을 함께 차량 설정에 맞춰 변경해야 한다.
- `data[3] & 0x80`는 해당 thermistor의 fault flag다.
- 한 프레임은 센서 하나다. 센서 45개가 활성화돼 있으면 100 ms 프레임을
  순환하므로 완전한 한 세트는 약 4.5초에 걸쳐 만들어진다.

프로토콜 근거:
[Orion Thermistor Module CANBUS](https://www.orionbms.com/downloads/misc/thermistor_module_canbus.pdf)

## 구성

- `orion_tem.py`: 순수 CAN 디코더
- `collector_tem_patch.py`: raw CAN 저장 뒤 decoded row를 같은 트랜잭션에 넣는
  integration helper
- `migrations/002_orion_tem_samples.sql`: additive PostgreSQL migration
- `tests/`: 외부 패키지와 DB 없이 실행되는 단위 테스트

## 운영 collector에 반영할 변경

운영 기준 파일은 `/opt/monolith/server/collector/collector.py`와
`schema.sql`이다. 전체 운영 파일에는 접속정보와 다른 운영 변경이 있으므로 이
저장소로 복사하지 않고 아래의 작은 변경만 적용한다.

1. `orion_tem.py`, `collector_tem_patch.py`를 collector build context에 복사한다.
2. `collector.py` import 구역에 다음을 추가한다.

   ```python
   from collector_tem_patch import store_can_and_optional_tem
   ```

3. `PARSER_VERSION`을 `1.1.0+monolith-v2.3.1-orion-tem`으로 올린다.
4. `_store_record()`의 기존 `if kind == "CAN":` insert를 다음 호출로 교체한다.

   ```python
   if kind == "CAN":
       store_can_and_optional_tem(cur, sid, raw_id, received_at, rec)
   ```

5. collector를 올리기 전에 `002_orion_tem_samples.sql`을
   `psql --single-transaction -v ON_ERROR_STOP=1`로 적용한다. 동일한 정상 schema에
   대한 재실행은 가능하지만, `IF NOT EXISTS`는 이름만 같은 불완전한 기존 table을
   교정하지 않는다. 적용 후 columns/constraints/indexes를 반드시 조회한다.
6. collector Dockerfile에 두 Python 파일이 포함되도록 COPY를 수정하고 이미지를
   다시 build한다. 호스트 파일만 바꿔서는 기존 이미지에 반영되지 않는다.

## 아날로그·디지털 샘플 제외 (2026-08-20 운영 적용)

TMA의 아날로그 6채널(`ain1~6`)과 디지털 4채널(`din1~4`)은 이 차량에서 배선하지
않았다. 값이 의미 없는데도 `samples` 행만 쌓여서 서버 decode 단계에서 버리도록
했다. 변경 내용은 `collector-skip-anl-dgt.patch`에 그대로 있다.

- `SKIP_SAMPLE_KINDS = {"ANALOG", "DIGITAL"}`를 추가하고 `parse_logbuf()`와
  `_store_record()` 양쪽에서 건너뛴다. `/d` logbuf 경로와 배열 토픽 경로 모두 막힌다.
- `PARSER_VERSION`을 `1.2.0+monolith-v2.3.1-orion-tem-no-anl-dgt`로 올렸다.
- GPS와 자이로는 그대로 저장한다.
- `raw_messages`에는 logbuf payload 원본이 계속 남는다. 되돌리려면 이 집합을 비우고
  build·재기동한 뒤 raw로 재처리하면 복구된다.
- 배포 전 `collector.py.bak-20260820`으로 백업했고, 운영 payload 표본으로 구·신 파서
  결과를 비교해 `ANALOG`/`DIGITAL`만 빠지는 것을 확인한 뒤 이미지를 다시 build했다.

이것은 **서버 저장만** 막는다. 장치는 여전히 ADC를 읽고 SD에 기록하며, ADC read
실패 시 `ADC_READ_FAIL` syslog를 계속 올린다(2026-08-18까지 49,531건, 전체 이벤트의
99.6%). 이 syslog는 `kind="SYSTEM"`이라 위 필터에 걸리지 않는다. 장치에서 멈추려면
`lighton/set/anl/en`과 `lighton/set/dgt/en`에 **바이너리 `0x00`**을 발행하고 재부팅해야
한다. 문자열 `"0"`은 `0x30`이라 참으로 해석되므로 쓰면 안 된다
(`mqtt.c`: `nvs_set_u8(nvs, "anl_en", evt->data[0] != 0)`).

## 저장 형태와 영향

`can_frames`는 계속 **서버에 도착한 범위에서** canonical raw 원본이다. target frame이면
`thermistor_samples`에 다음 projection을 한 행 더 만든다.

| 열 | 의미 |
|---|---|
| `can_frame_id` | 정확한 raw CAN 행 FK; 한 raw frame당 decoded row 1개 보장 |
| `session_id`, `raw_id` | 세션 및 MQTT 원본 추적 |
| `received_at` | 서버 UTC 수신시각 |
| `device_ts_ms` | TMA uptime 기반 원래 프레임 시각 |
| `module_address` | CAN ID의 J1939 source address (`0x80`) |
| `sensor_id` | zero-based local ID |
| `temperature_c` | signed °C |
| `fault` | thermistor fault bit |

CAN raw insert와 decoded insert는 같은 기존 배치 트랜잭션에서 수행되므로 decoded
insert가 실패하면 배치가 commit되지 않는다. 기존 spool checkpoint/replay가 해당
배치를 다시 처리한다. target이 아닌 CAN과 기존 `samples`/`events` 경로에는 동작
변화가 없다.

`can_frame_id` UNIQUE는 한 `can_frames` 행의 중복 projection을 막지만, 별도 수동
backfill이 원본 CAN 행 자체를 다시 INSERT하는 것까지 막지는 않는다. 운영 backfill은
기존 `can_frames.id`를 기준으로 decoded 행만 생성해야 한다.

`session_id`와 `raw_id`의 FK는 기존 `can_frames`와 같은 기본 동작(`NO ACTION`)을
유지한다. 즉 세션이나 MQTT 원본을 보존하는 동안 추적성이 유지되며, 보존정책에서
부모 행을 삭제하려면 먼저 연결된 raw/decoded 행을 정리해야 한다. 반면 decoded
row는 raw CAN에서 파생된 값이므로 `can_frame_id`에만 `ON DELETE CASCADE`를
명시했다.

## 테스트

저장소 루트에서:

```powershell
python -B -m unittest discover -s tools/MonolithServer/collector/tests -v
```

검증 항목은 양수·음수 온도, sensor/fault 비트 분리, extended/RTR/ID/DLC strict
matching, raw-first 저장, traceability 필드, non-target raw-only 경로다.

개발 PC에는 Docker/PostgreSQL 도구가 없어 단위 테스트만 수행했다. 운영 VM에서는
2026-08-14 migration 적용, image build, 컨테이너 내부 decoder probe, collector
재기동 및 schema marker 확인을 완료했다. 실차 TEM 데이터가 들어온 뒤 raw:decoded
1:1과 45 unique ID를 추가로 확인해야 한다.
