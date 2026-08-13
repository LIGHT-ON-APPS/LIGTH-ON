# LIGHT_ON Monolith Downloader

`MonolithDownloader.exe`를 더블클릭하면 다음 작업을 자동으로 수행한다.

1. 한국시간 오늘 날짜의 PostgreSQL `samples` 데이터를 CSV로 내보낸다.
2. CSV를 SSH/SCP로 `C:\project\LIGHT_ON\exports`에 내려받는다.
3. 연결된 이동식 SD 카드에서 `*.log`를 찾아 `exports\logs\날짜`에 복사한다.
4. 서버에서 만든 임시 CSV를 삭제한다.

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
