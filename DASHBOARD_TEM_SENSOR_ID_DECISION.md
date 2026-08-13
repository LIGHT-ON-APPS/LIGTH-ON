# 계기판 TEM 센서 번호 표시 철회 결정

## 결정 정보

| 항목 | 내용 |
|---|---|
| 상태 | 철회됨 |
| 결정일 | 2026-08-13 KST |
| 최종 동작 | 기존 `0x6B0` 최고 온도 1개 표시 유지 |
| 서버 45채널 수집 | 별도 진행 |

## 기존 동작

계기판의 `CELL TEMP`는 L432가 Orion BMS 사용자 설정 표준 CAN 프레임
`0x6B0`의 Byte 6~7에서 읽은 `High Temperature` 1개를 UART로 F411에 전달해
표시한다. 센서 번호는 포함하지 않는다.

## 검토했던 변경

다음 경로로 최고 온도 센서 번호 `T1~T45`를 추가하는 안을 검토했다.

```text
TEM 0x1839F380 summary
→ L432 telemetry decoder
→ UART 예약 byte + valid bit 17
→ F411 TouchGFX CELL TEMP의 T번호
```

## 철회 결정

이 변경은 최종 소스에 포함하지 않는다.

- 서버의 45채널 저장·CSV와 차량 계기판 변경을 분리한다.
- 기존 계기판은 이미 BMS가 계산한 최고 온도 1개를 표시한다.
- 실차에서 `0x1839F380`, source address 및 센서 설정을 확인하기 전에 차량
  MCU/UART/UI를 동시에 변경하지 않는다.
- 폰트·색상·레이아웃 개선은 데이터 프로토콜 변경 없이 별도 시안 승인 후 진행한다.

검토 중 추가했던 TEM summary decoder, sensor ID/fault 필드, UART bit 17,
TouchGFX `T번호` 표시는 모두 제거했다. `shared`, F411 및 TouchGFX의 최종 내용은
변경 전과 동일하며, `telemetry.c`에는 이번 TEM 계기판 기능과 무관한 기존 병행
작업만 남아 있다.

중간의 미완성 차량 펌웨어를 정상 기능 커밋으로 오인하거나 cherry-pick하지 않도록,
제안 코드를 먼저 기능 커밋한 뒤 revert하는 방식 대신 이 결정 기록을 독립 Git
커밋으로 남긴다.

## 재검토 조건

- 실차 CAN에서 `0x1839F380` 수신과 checksum 확인
- 최고 온도/센서 ID가 BMS Utility와 일치함을 확인
- stale/fault 상태에서 표시 정책 확정
- 480×320 화면 시안 승인
- L432/F411 혼합 버전 호환 및 실차 테스트 계획 승인

## 검증

- L432 Debug 전체 빌드: PASS
- F411 Debug 전체 빌드: PASS, 기존 RWX LOAD linker warning만 존재
- TEM 센서 번호 관련 잔여 source symbol 검색: 0건
- 최종 온도 경로: 기존 `0x6B0 → bms_max_temp_decic → CELL TEMP`
