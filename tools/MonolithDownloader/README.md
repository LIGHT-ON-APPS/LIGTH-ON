# LIGHT_ON Monolith Downloader

`MonolithDownloader.exe`를 더블클릭하면 다음 작업을 자동으로 수행한다.

1. 한국시간 오늘 날짜의 PostgreSQL `samples` 데이터를 CSV로 내보낸다.
2. CSV를 SSH/SCP로 `C:\project\LIGHT_ON\exports`에 내려받는다.
3. 연결된 이동식 SD 카드에서 `*.log`를 찾아 `exports\logs\날짜`에 복사한다.
4. 서버에서 만든 임시 CSV를 삭제한다.

서버에 Orion TEM decoder가 배포되어 있으면 같은 날짜의
`thermistors-YYYY-MM-DD.csv`도 함께 생성한다. 이 파일은 한 thermistor 순환을
한 행으로 묶고 `temp_01_c`~`temp_45_c`, `fault_01`~`fault_45`, 최고온도와
해당 센서 번호를 포함한다. 한 행은 동시 측정이 아니라 약 4.5초 동안 수집된
스냅샷이다. `complete=false`인 행의 빈 칸은 누락값이며 이전 값으로 보간하지
않는다.

운영 DB migration과 collector decoder는 2026-08-14 배포했다. 같은 날 실제 운영
DB에 대해 45개 온도 열과 45개 fault 열을 포함한 104컬럼 CSV 생성을 확인했다.
다만 TMA 10 Hz 펌웨어는 아직 플래시하지 않았고 배포 당시 장치도 오프라인이어서
검증 파일은 header-only였다. 실데이터 행과 `complete=true` 확인은 TMA 연결 후
진행한다.

스캔은 서버 수신 순서(`received_at`, DB 행 ID)에서 센서 ID가 감소하는 지점을
다음 순환의 시작으로 판단한다. 같은 센서 프레임이 중복되면 해당 순환에서 가장
나중에 수신된 값 하나만 사용한다. 따라서 센서 0 중복이나 다음 순환의 센서 0
누락이 있더라도 앞뒤 순환을 잘못 합치거나 쪼개지 않는다. 스캔 시간은 TMA의
32-bit millisecond timestamp wrap을 고려해 계산한다.

자정에 걸친 약 4.5초 스캔을 온전히 묶기 위해 조회 날짜 양쪽의 6초를 함께 읽되,
CSV에는 `scan_completed_at_kst`가 선택한 날짜에 속한 스캔만 기록한다. 같은
스캔이 이틀의 CSV에 중복 기록되지는 않는다.

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
