# Monolith collector Orion TEM patch

이 폴더는 운영 서버의 Monolith collector에 Orion Thermistor Expansion
Module(TEM) 개별 온도 프레임을 추가하기 위한 **로컬 검토·배포 패키지**다.
운영 VM의 코드나 DB에는 아직 적용하지 않았다.

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
  변환한다. 배포 전 실제 활성 ID 집합이 정확히 `0..44`인지 확인하고, sparse하면
  Downloader mapping을 차량 설정에 맞춰 변경해야 한다.
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

## 저장 형태와 영향

`can_frames`는 계속 모든 프레임의 canonical raw 원본이다. target frame이면
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

Docker/PostgreSQL 도구가 현재 개발 PC에 없어 실제 migration 실행과 collector
container 통합 시험은 배포 전 staging 또는 운영 VM에서 별도로 수행해야 한다.
