# LIGHT_ON Monolith Downloader

`MonolithDownloader.exe`를 더블클릭하면 다음 작업을 자동으로 수행한다.

1. 한국시간 오늘 날짜의 PostgreSQL `samples` 데이터를 CSV로 내보낸다.
2. CSV를 SSH/SCP로 `C:\project\LIGHT_ON\exports`에 내려받는다.
3. 연결된 이동식 SD 카드에서 `*.log`를 찾아 `exports\logs\날짜`에 복사한다.
4. 서버에서 만든 임시 CSV를 삭제한다.

서버에 Orion TEM decoder가 배포되어 있으면 같은 날짜의
`thermistors-YYYY-MM-DD.csv`도 함께 생성한다. 이 파일은 한 thermistor 순환을
한 행으로 묶고 `temp_01_c`~`temp_45_c`, `fault_01`~`fault_45`,
`measurement_device_ts_01_ms`~`measurement_device_ts_45_ms`, 최고온도와 해당
센서 번호를 포함한다. 전체 150컬럼이며, 한 행은 동시 측정이 아니라 약 4.5초 동안 수집한 뒤
TMA에서 동결한 스냅샷이다. TMA가 서버 전송을 2 Hz로 제한하면 이 한 행이 서버에
모이는 데 정상적으로 약 22초가 걸린다. 새 TMA 펌웨어는 `0..44`를 모두 수집한
스냅샷만 동결하므로 정상 전송에서는 완전한 행을 만든다. MQTT 전송 유실 또는
기존 펌웨어 데이터로 `complete=false`인 행이 생기면 빈 칸은 누락값이며 이전
값으로 보간하지 않는다.

운영 DB migration과 collector decoder는 2026-08-14 배포했다. 같은 날 실제 운영
DB에 대해 45개 온도 열과 45개 fault 열을 포함한 기존 CSV 생성을 확인했다.
다만 TMA 2 Hz 동결 스냅샷 펌웨어는 아직 플래시하지 않았고 배포 당시 장치도 오프라인이어서
검증 파일은 header-only였다. 실데이터 행과 `complete=true` 확인은 TMA 연결 후
진행한다.

스냅샷은 서버 수신 순서(`received_at`, DB 행 ID)에서 다음 중 하나가 발생하면
새로 시작한다.

- 센서 ID가 감소한다. 정상 순서는 `0..44`다.
- 연속 프레임의 원수집 `device_ts_ms` 간격이 6초를 초과한다.

두 번째 조건은 앞 스냅샷의 뒤쪽 센서와 다음 스냅샷의 앞쪽 센서가 함께 유실돼
센서 ID가 계속 증가해 보이더라도 서로 다른 스냅샷을 합치지 않기 위한 것이다.
같은 스냅샷에서 같은 센서 프레임이 중복되면 가장 나중에 서버가 받은 값 하나만
사용한다. 시간 차이는 TMA의 32-bit millisecond timestamp wrap을 고려한다.

주요 메타데이터 열의 뜻은 다음과 같다.

| 열 | 의미 |
| --- | --- |
| `snapshot_no` | 해당 CSV 조회 범위에서 재구성한 순번이며 장치가 부여한 영구 ID는 아니다. |
| `server_delivery_started_at_kst` | 스냅샷의 첫 프레임을 서버가 받은 한국 시각 |
| `server_delivery_completed_at_kst` | 스냅샷의 마지막 프레임을 서버가 받은 한국 시각 |
| `server_delivery_duration_ms` | 서버가 첫 프레임부터 마지막 프레임까지 받는 데 걸린 시간으로 2 Hz 전송 시 약 22,000 ms다. |
| `measurement_started_device_ts_ms` | 첫 센서를 TMA가 원수집한 장치 uptime 시각 |
| `measurement_completed_device_ts_ms` | 마지막 센서를 TMA가 원수집한 장치 uptime 시각 |
| `measurement_span_ms` | 45개 원수집 시각 범위로 정상값은 약 4,400 ms다. |
| `measurement_device_ts_01_ms`~`measurement_device_ts_45_ms` | 각 센서를 실제 수집한 TMA uptime 시각이며 누락 센서는 빈 칸이다. |
| `complete` | 센서 `0..44`가 모두 있고 `measurement_span_ms <= 6000`이면 `true`다. 서버 전달 시간은 이 판정에 사용하지 않는다. |

`server_delivery_*`는 측정시각이 아니라 collector의 서버 수신시각이다. 안정 연결에서는
약 22초지만, MQTT 재연결 후 backlog가 배치로 전달되면 훨씬 짧거나 불규칙하게 보일
수 있다. 이 경우에도 실제 원수집 순서와 범위는 `measurement_device_ts_*_ms`로 판단한다.

2 Hz 전달 중 자정이 지나도 스냅샷을 온전히 묶기 위해 조회 날짜 양쪽의 35초를
함께 읽는다. 장치 uptime을 절대시각으로 직접 바꿀 수 없으므로 CSV 날짜 귀속은
`server_delivery_completed_at_kst`를 기준으로 한다. 같은 스냅샷이 이틀의 CSV에
중복 기록되지는 않는다.

35초 여유는 정상적인 약 22초 순차 전달과 짧은 변동을 위한 값이다. Wi-Fi 단절이나
큐 적체로 한 스냅샷 전달이 35초보다 오래 멈추면 날짜 경계에서 완전한 행으로
재구성되지 않을 수 있다. 조회 범위 안에 일부 프레임이 있으면 `complete=false`,
전부 범위 밖이면 스냅샷 행 자체가 없을 수 있으므로 원본 SD 로그를 함께 확인한다.

서버에 `thermistor_samples` migration이 아직 없으면 기존 telemetry CSV와 SD
복사는 계속 수행되고, GUI/로그에 45채널 CSV 경고만 남는다.

GUI에서 날짜와 저장 폴더를 바꾼 뒤 `지금 다운로드`를 눌러 다시 실행할 수도 있다.

## 필요 조건

- SSH 개인 키: `%USERPROFILE%\.ssh\oracle_monolith.key`
- host key: 저장소 루트의 `.oracle_known_hosts`
- Windows OpenSSH의 `ssh.exe`, `scp.exe`
- Oracle VM의 Monolith/PostgreSQL 스택이 실행 중이어야 한다.

## `.log` 제한

PostgreSQL 데이터를 Monolith 원본 `.log`로 재구성하지 않는다. `.log`는 TMA SD 카드의 원본이다.
현재 OCI TCP 80 인바운드가 막혀 있어서 보드의 원격 파일 업로드는 동작하지 않는다. 따라서 이 프로그램은
PC에 연결된 이동식 SD 카드 또는 사용자가 선택한 SD 폴더에서 `.log`를 복사한다.

SD 카드가 이동식 드라이브로 잡히지 않으면 GUI의 `SD 폴더(선택)`에서 드라이브 루트를 직접 지정한다.

## 다시 빌드

PowerShell에서:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  "C:\project\LIGHT_ON\tools\MonolithDownloader\build.ps1"
```

빌드 결과는 저장소 루트의 `MonolithDownloader.exe`다.

생성된 SQL의 150컬럼과 스냅샷 조건을 로컬에서 확인하려면 빌드 후 실행한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  "C:\project\LIGHT_ON\tools\MonolithDownloader\tests\test_generated_sql.ps1"
```

`tests\test_thermistor_snapshot_grouping.sql`은 PostgreSQL용 읽기 전용 회귀
테스트다. 테이블을 만들거나 운영 데이터를 변경하지 않으며 2 Hz 전달, 누락 경계,
32-bit timestamp wrap, 자정 경계를 검증한다.

## 자동 검증용 실행

GUI를 띄우지 않고 CSV만 받는 옵션:

```powershell
Start-Process -Wait -PassThru `
  "C:\project\LIGHT_ON\MonolithDownloader.exe" `
  -ArgumentList @(
    "--headless",
    "--date", "2026-08-12",
    "--output", "C:\project\LIGHT_ON\exports",
    "--no-sd"
  )
```
