# Monolith 텔레메트리 구축 기록

`ORACLE_MONOLITH_LOGGER.md`(설계 매뉴얼)에 따라 실제로 구축한 결과와, 그 과정에서 막혔던 지점을 남긴다.
설계 근거는 매뉴얼을, **실제로 무엇이 어떻게 되어 있는지와 왜 그렇게 했는지는 이 문서**를 본다.

최종 갱신: 2026-08-14 (TEM 서버 배포 및 2 Hz snapshot 설계 기록 추가)

---

## 1. 최종 구성

```
STM32L432KC ──CAN 500k──┬── Bamocar (0x201 / 0x181)
  APPS 제어              └── TMA-1 (Monolith v2, ESP32-S3)
                                │
                                ├── SD 카드 (CAN 원수신 주기)
                                │
                                └── wss://168.110.48.114.sslip.io:443
                                          │
                    ┌─────────────────────┴──────────────────────┐
                    │  Oracle Cloud VM (LIGHT_ON_Monolith)        │
                    │                                            │
                    │  traefik ──┬── nginx ── Control Hub (web)   │
                    │   (TLS)    └── mqtt-proxy ── mosquitto      │
                    │                                  │         │
                    │                             collector      │
                    │                                  │         │
                    │                             PostgreSQL     │
                    └────────────────────────────────────────────┘
                                          │
                              OCI Object Storage (백업)
```

### 서버 2대

| | Monolith 서버 | 퀀트 서버 |
|---|---|---|
| IP | `168.110.48.114` | `131.186.39.76` |
| 호스트명 | `light-on-monolith` | `quant` |
| 사양 | VM.Standard.E2.1.Micro (1 OCPU / 954 MB) | 동일 |
| 용도 | Monolith 전용 | `quantbot.service` 전용 |

처음에는 퀀트 서버에 얹었다가 전용 VM으로 분리했고, **퀀트 서버는 Docker 패키지까지 purge해 완전히 원상복구**했다.
Ampere A1(24 GB)을 노렸지만 도쿄 리전은 단일 AD이고 A1 재고가 없어 E2.1.Micro 2대째로 갔다.

접속:
```bash
ssh -i ~/.ssh/oracle_monolith.key \
  -o UserKnownHostsFile=/c/project/LIGHT_ON/.oracle_known_hosts \
  ubuntu@168.110.48.114
```

### 컨테이너 7개

| 컨테이너 | 출처 | 역할 |
|---|---|---|
| `traefik` | upstream | TLS 종료(443), Let's Encrypt |
| `nginx` | upstream | Control Hub 서빙, MQTT WebSocket 프록시 |
| `mqtt-proxy` | upstream | haproxy → mosquitto |
| `mosquitto` | upstream | MQTT 브로커 |
| `file-cleanup` | upstream | 업로드 파일 정리 |
| **`postgres`** | 직접 구축 | 영구 저장 (매뉴얼 §7) |
| **`collector`** | 직접 구축 | MQTT 수신 → 파싱 → 적재 |

기반: Monolith v2.3.1, 커밋 `5fbcdf3` 고정.
`monolith.service` enabled → VM 재부팅 시 자동 기동.

---

## 2. upstream 교정 사항

upstream 파일은 **수정하지 않고** `config-docker/` + `compose.production.yaml` 오버레이로 덮는다.
업스트림을 올릴 때 이 목록을 diff 해야 한다.

| # | 문제 | 원인 | 조치 |
|---|---|---|---|
| 1 | nginx 기동 실패 | upstream이 전체 설정을 `conf.d/default.conf`에 마운트 → `http{}` 안에서 include되어 "user directive is not allowed here" | 최상위 `/etc/nginx/nginx.conf`로 마운트 |
| 2 | MQTT WebSocket 항상 실패 | resolver가 `10.89.0.1`(Podman 전제) | `127.0.0.11`(Docker DNS)로 교정 |
| 3 | mosquitto 기동 실패 | `enable_proxy_protocol`은 2.1+ 전용인데 `eclipse-mosquitto:2.1` 이미지가 없음 | 해당 지시어 + haproxy의 `send-proxy-v2` **둘 다** 제거 (짝이므로 함께) |
| 4 | traefik 라우터 미생성 | traefik의 docker provider가 API 1.24 고정 → Docker Engine 29(MinAPI 1.40)와 통신 불가. `DOCKER_API_VERSION`으로도 안 됨 | docker provider → **file provider**로 교체(`traefik-dynamic.yml`). 부수 효과로 `docker.sock` 마운트 제거 |
| 5 | postgres unhealthy | `/srv/monolith/postgres` 소유자 불일치 | `chown -R 999:999` |
| 6 | collector 권한 오류 | 이 VM의 `ubuntu`는 **uid 1001**(구서버는 1000) | `COLLECTOR_UID` 변수화 |
| 7 | mosquitto 로그 0바이트 | `/srv/monolith/log`가 0750이라 uid 1883이 진입 불가 | `chmod 0755` |

> ⚠️ 마운트된 설정 파일의 **내용만** 바꾸면 compose가 컨테이너를 재생성하지 않는다. `up -d --force-recreate` 필요.

---

## 3. 데이터 파이프라인 (매뉴얼 §7)

upstream Compose에는 DB가 없어 실시간 중계만 되고 주행 이력이 남지 않는다(매뉴얼 §4). 그래서 직접 만들었다.

### 설계

```
MQTT 스레드 ──(원본 그대로 append)──> spool 파일 ──> worker 스레드 ──> PostgreSQL
                                        ↑
                                   checkpoint
```

MQTT 콜백은 **원본만 spool에 적고 즉시 반환**한다. 파싱·적재는 별도 worker가 한다.
DB가 죽어도 수신은 계속되고, 복구되면 checkpoint부터 재생한다.

### 스키마

`schema_meta`, `devices`, `sessions`, `raw_messages`, `can_frames`, `samples`, `events`,
`thermistor_samples` + `collector_health` 뷰.

원본은 `raw_messages`에 **파싱 성공 여부와 무관하게** 보존한다. 파서를 고치면 과거 데이터를 다시 해석할 수 있다.

### 배포 주의

collector는 `build: context: ./collector`로 **코드가 이미지에 구워진다.**
호스트 파일만 고치면 반영되지 않는다:

```bash
cd /opt/monolith/server
docker compose -f docker-compose.yml -f compose.production.yaml build collector
docker compose -f docker-compose.yml -f compose.production.yaml up -d collector
```

---

## 4. 백업과 모니터링 (매뉴얼 §9, §10)

### 백업 — `/opt/monolith/ops/backup.sh`, 매일 03:30 UTC

1. `pg_dump` + sha256
2. 처리 완료된 spool 세그먼트 압축 + sha256 (checkpoint가 가리키는 파일은 건드리지 않음)
3. OCI Object Storage 업로드 (`monolith-backup` 버킷, `db/`·`raw/`)
4. **업로드 성공을 확인한 뒤에만** 로컬 보존 정책 적용 (14일)

Object Storage는 **instance principal**로 인증한다(VM에 user API key를 두지 않음, 매뉴얼 §9).

```
dynamic group : 'Default'/'monolith-instances'
                규칙 ALL {instance.compartment.id = '<tenancy>'}
정책 1        : Allow dynamic-group 'Default'/'monolith-instances' to manage object-family in tenancy
정책 2        : Allow service objectstorage-ap-tokyo-1 to manage object-family in tenancy
lifecycle     : raw/ 30일 후 ARCHIVE, 전체 365일 후 DELETE
```

> **Identity Domains 테넌시는 정책에서 `'도메인'/'그룹'` 형식을 써야 한다.** 예전 문법을 쓰면 그룹을 못 찾아 정책이 조용히 무효가 된다.
>
> lifecycle 적용 시 `InsufficientServicePermissions`가 나는데, OCI가 대신 객체를 지우는 기능이라 **서비스 principal 권한(정책 2)이 별도로** 필요하다. Always Free는 20 GB 한도이고 telemetry 10 ms면 0.9 GB/day라 이 정책이 없으면 22일이면 찬다.

### 모니터링 — `/opt/monolith/ops/healthcheck.sh`, 5분 주기

`/srv/monolith/health.json`에 지표를 쓰고, 임계 초과 시 unit이 failed로 남는다(`systemctl --failed`).

```
MAX_MESSAGE_AGE_S=900     # 보드 미연결 기간엔 86400으로 완화했다가 연결 후 복귀
MAX_BACKUP_AGE_S=93600
MIN_DISK_FREE_PCT=15
```

### 검증한 것

- **장애 시험**: postgres만 정지 → spool 증가 → 복구 후 전량 replay, 중복 0
- **손상 payload 주입**: parser 생존 + raw 보존
- **재해복구 훈련**: Object Storage만으로 전체 복원 **25초**, sha256·gzip 무결성 확인, 행수 완전 일치
- systemd 정지/시작 사이클, wss 경로 end-to-end

---

## 5. L432 진단 CAN 프레임

APPS 상태를 CAN에 실어 TMA-1이 기록할 수 있게 했다(매뉴얼 §3).

| ID | 내용 | 주기 |
|---|---|---|
| `0x7A0` | 원시 ADC 2채널, 최종 페달값, 상태 플래그 | 20 Hz |
| `0x7A1` | 보정된 위치 2채널, Bamocar 토크 명령 | 20 Hz |
| `0x7A2` | 상태/fault 비트마스크, 진단 송신 실패 누적 | 20 Hz |

- 기준 정의: `shared/apps_diag_can.h` (인코더 + 범위 검증 포함)
- 골든 벡터 시험: `shared/test_apps_diag_can.c` (8건, `-Wall -Wextra -Wconversion -Wsign-conversion` 통과)
- CAN analyzer / Control Hub decoder용: `shared/apps_diag_can.dbc`
- 세 프레임이 **같은 rolling counter**를 공유(byte 7) → 주기 누락 감지

### 안전 원칙

```c
/* 진단은 안전 경로에 개입하지 않는다. _DEF_VER1에서 타당성 fault가 확정되면
 * appsSendCommand()가 canClose()로 토크 명령 경로를 물리적으로 끊는데, 진단이
 * 채널을 다시 열면 그 보장이 사라진다. 닫혀 있으면 이 주기를 건너뛴다. */
if (canIsOpen(APPS_CAN_CH) != true) { return; }
```

송신은 제어 루프와 **위상을 반주기 어긋나게** 배치해 10 ms 데드라인에 영향을 주지 않도록 했다.

### CAN ID 충돌 조사

저장소와 git 이력에서 복구한 검증 문서 기준, 쓰이는 표준 ID는 `0x181`/`0x201`(Bamocar), `0x6B0`(Orion BMS)뿐이라 `0x7A0~0x7A2`와 겹치지 않는다.
500 kbit/s에서 진단 3프레임 20 Hz는 버스 부하 **약 1.6%**.

> 아직 확인 못 한 것: 팀의 **전체 차량 CAN ID 표**. 저장소에 없는 다른 ECU가 있다면 대조가 필요하다.

---

## 6. 보드 연결 트러블슈팅 (2026-07-31)

가장 오래 걸린 부분이라 **틀린 추론까지 포함해** 남긴다. 증상이 원인을 강하게 오도한다.

### 증상

- 보드는 "전송 중", 핫스팟에는 데이터 사용량이 잡힘
- 서버 mosquitto 로그에 **접속 기록 0건**
- traefik 액세스 로그에도 없음
- 보드 LED 빠른 깜빡임

### 진짜 원인 — TLS 입력 버퍼 초과

```
CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN = 4096 bytes
RSA 인증서 체인 Certificate 메시지 = 4339 bytes
→ mbedtls_ssl_handshake returned -0x7200
```

Let's Encrypt가 2026-01부터 "Generation Y" 계층으로 발급하는데, RSA 체인
(`leaf → YR1 → Root YR(X1 교차서명)`)이 4096 버퍼에 안 들어간다.
보드는 인증서를 **전부 수신·ACK한 뒤 RST**로 끊는다.

### 해결 — 서버 인증서를 EC256으로

```
--certificatesresolvers.le.acme.keytype=EC256
```

ECDSA 기본 체인은 `EE ← YE1 ← Root YE ← ISRG Root X2 ← ISRG Root X1`로
**X1에 종단되면서도 약 3.4 KB**라 버퍼에 들어간다. ISRG Root X2가 X1으로 교차 서명돼 있는 덕분이다.
**펌웨어 재플래싱 불필요.**

### 왜 이렇게 헤맸나 — 오도하는 지점 4개

| 관측 | 잘못된 해석 | 실제 |
|---|---|---|
| mosquitto 로그에 아무것도 없음 | 보드가 서버에 도달 못 함 | TLS가 WebSocket 업그레이드 **전에** 끝나서 안 남을 뿐 |
| 핫스팟에 송신 기록 | 다른 서버로 보내는 중 | TCP+TLS 핸드셰이크 바이트는 실제로 나감 |
| TLS alert 없이 RST | 인증서 **검증** 실패 | 버퍼 부족으로 레코드 처리 자체가 불가 |
| curl/브라우저는 정상 | 서버 TLS 정상이므로 서버 문제 아님 | 시스템 트러스트 스토어를 쓰는 클라이언트라 **보드 조건을 검증한 게 아님** |

### 실패한 시도들 (다시 하지 말 것)

- **`preferredChain=ISRG Root X1` 단독** — RSA 신규 발급에는 X1 종단 대체 체인이 더 이상 제공되지 않는다. 재발급해도 Gen-Y가 나온다(중간만 YR2→YR1로 바뀜). EC256과 함께 쓸 때만 의미가 있다.
- **`classic` 프로필** — 이미 기본값이고 그것도 Gen-Y를 준다. traefik 3.4.5엔 `profile` 옵션 자체가 없다(v3.5+).
- **교차서명 루트를 체인 끝에 덧붙이기** — 이미 들어 있었다. 중복만 생기고 체인이 더 커졌다.
- **체인에서 교차서명 루트 제거** — YR1↔X1을 잇는 유일한 고리라 구조적으로 불가.

### 결정적이었던 진단 — 패킷 캡처

서버 로그만으로는 절대 안 보인다.

```bash
sudo tcpdump -nn -i ens3 'tcp port 443 and net <보드대역>'
```

```
SYN → SYN-ACK → ACK              TCP 연결 성립
→ 178 bytes                       ClientHello
← 2744 + 2271 bytes               ServerHello + 인증서 (5016)
→ ACK 5016                        전부 수신
→ RST                             즉시 강제 종료      ← 여기가 답
```

3.5초 주기로 정확히 반복 = 재연결 루프. ClientHello의 SNI 문자열로 보드가 **어느 호스트명으로 오는지**도 확인할 수 있다.

### 확인 순서 (다음에 막히면 이 순서로)

1. `tcpdump`로 보드 패킷이 오는가 → 안 오면 Wi-Fi/DNS/`server` 설정
2. 오는데 RST면 → TLS. 인증서 체인 크기와 종단 루트 확인
3. mosquitto에 접속은 되는데 데이터가 없으면 → ACL/`name` 불일치
4. 시리얼 콘솔로 `mbedtls_*` 오류 코드 확인

---

## 7. TMA-1 보드 참고

### 설정 (AP 모드)

AP는 **SSID가 없거나 비번이 8자 미만일 때만** 열린다(`network.c:103`).

```
SSID     : Monolith v2 XXXXXX   (MAC 마지막 3바이트)
비밀번호 : monolith
주소     : http://192.168.4.1
```

입력 가능한 필드는 **5개뿐**(`webserver.c:104`): `ssid`, `passwd`, `server`, `name`, `key`.

- `server`: **호스트명만** (`https://`나 `:443`을 붙이면 `wss://https://...:443`이 되어 실패)
- `name`: **MQTT username 겸 토픽 접두사**. ACL이 `pattern read/write %u/#`라 채널명과 다르면 접속은 되는데 publish만 조용히 버려진다
- `key`: **31자 제한**. 32자 키는 조용히 잘려 인증만 실패한다 (`openssl rand -hex 14` = 28자)

> Wi-Fi 비번이 틀리면 `portMAX_DELAY`로 영원히 블록되고 **AP로 돌아오지 않는다**. 복구는 **GPIO 21을 3초 이상**(`nvs_erase_all` + 재부팅).

### CAN 기본값 — 건드릴 필요 없음

AP 페이지에 CAN 항목이 없어서 잘못 설정할 방법이 없다.

```
can_bps    = CAN_BPS_500K    ✅ 우리 버스와 일치
can_filter = 0x00000000      ✅ accept all
can_mask   = 0xFFFFFFFF      ✅ 0x7A0~0x7A2 수신됨
can_en     = true
```

> upstream 버그(`main.c:243`): 기본값을 `can_bps`가 아니라 `can_en` 키에 쓴다. `CAN_BPS_500K`가 11이라 truthy로 남아 우리 설정에선 무해하지만, 500K 외 속도는 저장이 안 된다.

### SD vs 서버 — 수신율이 다르다

```c
if (last && now - *last >= interval && canlogqueue)   // can.c:170, interval = intv = 500ms
```

| 경로 | 진단 프레임 |
|---|---|
| **SD 카드** | CAN 원수신 주기로 기록 (`LOG()` 경로 유지; 진단 프레임 예시는 20 Hz, TEM은 약 10 Hz) |
| **서버 (live)** | 기본 CAN ID당 **2 Hz**. Orion TEM은 45개 frozen snapshot을 최대 2 Hz로 순차 전달하는 패치가 준비됐으나 아직 TMA에 미플래시 |

서버에서 일반 CAN의 20 Hz가 안 보이는 건 정상이다. 정밀 분석은 SD 원본, 서버는 실시간 감시·장기 보관용.

Orion TEM 45채널 서버 수집의 설계, 운영 영향, 배포 및 롤백 절차는
`TEM_45CH_DATA_PIPELINE_CHANGE.md`를 따른다. DB migration과 collector는
2026-08-14 운영 VM에 배포했다. TMA 패치는 원본 CAN의 연속 ID `0..44` 한 순환을
동결하고 센서별 원래 timestamp를 보존한 채 500 ms 간격, 최대 2 Hz로 전달한다.
작업 PC에 보드가 연결되어 있지 않아 아직 플래시하지 않았다. 따라서 현재는 서버
schema와 decode/export 경로만 준비된 상태이며, 차량 CAN 원본 약 10 Hz와 서버
최대 2 Hz 전달에서 45채널 `complete=true`가 되는 실데이터 검증은 남아 있다.

### LED

부팅 시 **모든 컴포넌트가 FATAL로 시작**해 각 init 성공마다 해제된다(`main.c:49`).
MQTT는 **브로커에 실제로 붙어야** 해제되므로, 미연결 상태의 빠른 깜빡임은 정상이다.

| 3초간 | 주기 | 의미 |
|---|---|---|
| 약 2회 | 1000 ms | 전부 정상 |
| 약 6회 | 250 ms | ERROR (GPS 미수신 등) |
| 약 15회 | 100 ms | FATAL (Wi-Fi/MQTT 미접속, SD, RTC, NVS) |

### 시리얼 콘솔

**921600 bps**, 8-N-1, UART0 **GPIO 43(TX) / 44(RX)**.
`CONFIG_LOG_DEFAULT_LEVEL=2`(WARN)라 `ESP_LOGI`는 안 나오고 WARN/ERROR만 나온다.
115200으로 열면 아무것도 안 보인다. 포트는 배타적 접근이라 다른 도구가 잡고 있으면 0바이트만 읽힌다.

### 원격 설정 (MQTT)

`<name>/set/<그룹>/<키>`로 NVS를 바꾼다. **NVS에만 쓰고 다음 부팅부터 적용**된다(`mqtt.c:281`).

| 그룹 | 키 |
|---|---|
| `net` | `ssid`, `passwd` |
| `dev` | `tz`, `intv`(u32) |
| `can` | `en`, `bps`, `filter`(u32), `mask`(u32) |
| `gps` | `en`, `dev` |
| `anl` | `en` |
| `dgt` | `en` |

성공 시 `<name>/ack/set`에 `ok`. `<name>/cmd/rbt`로 원격 재부팅까지 되므로 설정 반영에 보드 접근은 필요 없다.

> mosquitto는 `listener 9001` + `protocol websockets` **하나뿐이라 `mosquitto_pub`으로는 발행할 수 없다**(TCP 1883 리스너 없음). collector 이미지의 paho-mqtt(`transport="websockets"`)를 쓴다.
> `en` 계열 payload는 **바이너리 `0x00`/`0x01`**이다. 펌웨어가 `evt->data[0] != 0`으로만 보므로 문자열 `"0"`(0x30)을 보내면 오히려 켜진다.

---

## 8. Control Hub (웹)

**`https://168.110.48.114.sslip.io/`**

페이지는 우리 서버에서 받아오지만 **MQTT 접속 대상은 브라우저 localStorage에 따로 저장된다**(`mqtt.js:30`).
그냥 열면 upstream 서버로 붙으려 해서 데이터가 안 뜬다.

**Device 페이지 → Server 카드**에서 3개를 입력한다:

| 필드 | 값 |
|---|---|
| Address | `168.110.48.114.sslip.io` |
| Name | `lighton` |
| Key | 서버 `/opt/monolith/server/.env`의 `CHANNEL_KEY` |

각 필드마다 저장 버튼이 따로 있다. `save()`가 매번 재접속을 시도하므로 **Key를 마지막에** 저장한다.
인증 실패 시 자동 재접속이 멈추므로(`mqtt.js:57`) 다시 저장 버튼을 눌러야 재시도한다.

배포된 번들의 기본 주소는 우리 서버로 패치해 뒀다(`/opt/monolith/web/dist/assets/index-*.js`, 원본은 `.orig`로 백업).
단 **이미 방문한 브라우저는 localStorage에 이전 값이 남아 있어** UI에서 직접 바꿔야 한다.

> Key는 일부러 번들에 넣지 않았다. 공개 URL이라 Key가 노출되면 텔레메트리 열람은 물론 `set`/`cmd` 토픽으로 **보드 원격 조작**(설정 변경, 파일 삭제)이 가능해진다.

---

## 9. 직접 만든 버그 (재발 주의)

| # | 버그 | 증상 | 수정 |
|---|---|---|---|
| 1 | spool reader가 읽으면서 메모리 offset을 전진 | DB 실패 시 그 구간이 **영영 유실** | 두 `except` 블록에 `rewind_to_checkpoint()` 추가. DB 장애 훈련으로 발견 |
| 2 | 데이터를 `boot_epoch=NULL` 세션에 적재 | 주행 세션 단위 조회 불가 | `current_session()` 추적 도입 |
| 3 | 백업 업로드가 `*.gz`만 매칭 | `.sha256`이 원격에 없어 **복원 시 무결성 확인 불가** | 글롭에 `*.gz.sha256` 추가 |
| 4 | 채널 키를 `rand -hex 16`(32자)으로 생성 | `char key[32]`에 잘려 인증 실패 | `rand -hex 14`(28자)로 재생성 |
| 5 | `parse_boot`가 4바이트만 허용 | 8바이트 boot 레코드를 거부 → **세션이 생성되지 않음** | 4/8바이트 모두 처리 + epoch 범위 검증 |

### 5번 상세

펌웨어는 `(char *)&boot.tv_sec`를 `sizeof(time_t)`만큼 보내는데(`mqtt.c:516`),
**ESP-IDF v5의 `time_t`는 64비트**다. 4바이트만 가정하면 boot 레코드가 전부 버려지고
CAN/샘플이 엉뚱한 세션에 묶인다.

boot 메시지는 **retained**라 collector를 재시작하면 즉시 재전달되어 세션이 복구된다.

---

## 10. 잘못 짚었던 진단 (기록용)

같은 함정을 피하기 위해 남긴다.

| 주장 | 실제 | 교훈 |
|---|---|---|
| "443이 TIMEOUT이니 OCI가 막고 있다" | iptables `REJECT --reject-with icmp-host-prohibited`가 ICMP 필터 환경에서 TIMEOUT으로 보인 것 | 거부 방식에 따라 증상이 달라진다. 임시 컨테이너로 직접 확인해야 한다 |
| "체인이 자체 서명 Root YR로 끝난다" | 처음부터 X1으로 교차 서명돼 있었다 | `-showcerts` 출력에서 **issuer 줄을 잡는 정규식이 틀려** subject만 보고 단정했다 |
| "보드 시계가 안 맞아 인증서 날짜 검증이 실패한다" | `RTC_SNTP_SYNC` 정상, 시계 맞음 | 그럴듯한 가설이었지만 **확인 없이 원인으로 단정**했다 |
| "ECDSA는 X2로 앵커되니 불가" | X2가 X1으로 교차 서명돼 있어 **가능했고, 이게 정답이었다** | 확인 없이 배제하면 정답을 버린다 |

공통점: **관측하지 않은 것을 관측한 것처럼 다뤘다.** 체인은 `openssl`로 subject+issuer를 모두 찍고,
연결은 `tcpdump`로 실제 패킷을 보고 판단해야 했다.

---

## 11. 현재 상태 (2026-07-31 10:32 UTC)

```
컨테이너      7개 전부 Up, postgres healthy
health.json   status: ok, alerts: []
              last_message_age_seconds: 1
              spool_pending_bytes: 0
디스크        39 GB 여유 (13% 사용)
메모리        297 MB 여유
인증서        EC256, ISRG Root X1 종단, 2026-09-14 만료
보드          ESP32_d10334 접속 중
```

| 테이블 | 행수 |
|---|---|
| `raw_messages` | 9,150 |
| `samples` | 10,864 |
| `events` | 24,984 |
| `can_frames` | 8 (L432 미연결이라 정상) |
| `sessions` | 8 |
| DB 크기 | 18 MB |

---

## 12. 남은 작업

### 급함

- [ ] **`ADC_READ_FAIL` 폭주** — 아날로그 센서 미연결인데 `anl_en` 기본값이 true라 **분당 약 640건**(전체 이벤트의 99.6%, 하루 약 1 GB). `lighton/set/anl/en`에 `0x00` 발행 후 재부팅하면 꺼진다.
  2026-08-20 기준 누적 49,531건. 아날로그·디지털을 안 쓰기로 해서 **서버 저장은 이미 막았지만**(collector `SKIP_SAMPLE_KINDS`), 이 syslog는 `kind=SYSTEM`이라 안 걸린다. 장치를 꺼야 멈춘다.
  TMA가 8/18부터 오프라인이라 발행은 대기 중. 온라인 되면
  `docker exec monolith-v2-collector-1 python /tmp/tma_disable_anl_dgt.py --reboot`
  (스크립트는 `tools/MonolithServer/ops/`와 서버 `/home/ubuntu/ops/`에 있다)
- [ ] **Control Hub 접속 설정** — Device 페이지에서 3개 입력 (§8)

### 보통

- [ ] **L432 펌웨어 빌드·플래싱** — 진단 CAN 코드는 커밋됐지만 빌드 산출물이 없다. 플래싱 전에는 `0x7A0~0x7A2`가 버스에 나오지 않는다
- [ ] **CAN 실물 검증** — 종단 저항 60 Ω 확인(TMA-1은 `TWAI_MODE_NORMAL`이라 능동 노드다. 40 Ω이면 TMA-1에 종단이 붙어 있는 것), analyzer로 주기·DLC·counter 확인
- [ ] **인증서 갱신 확인** — 45일 만료(9/14)라 8월 중순 갱신 시 EC256 설정이 유지되는지 봐야 한다
- [ ] **팀 전체 CAN ID 표와 대조** (§5)

### 나중

- [ ] **월 1회 정기 restore 테스트 자동화** — 매뉴얼 §9 요구사항. 현재는 수동 1회만 수행
- [ ] **OCI TCP 80 인바운드** — 원격 `.log` 다운로드용. 단 upstream 경로가 **평문 HTTP**라 주행 로그가 암호화 없이 전송된다(매뉴얼 §5). 열지 여부는 팀 판단
- [ ] **펌웨어 TLS 버퍼 상향** — `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384`. 지금은 EC256으로 우회했지만 LE 체인은 2026년에만 두 번 커졌다. `DYNAMIC_BUFFER=y`라 상한일 뿐 상시 점유가 아니다
- [ ] `rpcbind`(포트 111) 정리

---

## 13. 자주 쓰는 명령

```bash
# 상태 한눈에
ssh ... 'docker ps --format "{{.Names}}\t{{.Status}}"; cat /srv/monolith/health.json'

# 데이터 유입 확인
docker exec -i monolith-v2-postgres-1 psql -U monolith -d monolith -c \
  "select topic, parse_status, count(*), max(received_at) from raw_messages group by 1,2 order by 3 desc;"

# 세션 확인 (boot_epoch이 NULL이면 안 된다)
... -c "select id, boot_epoch, started_at, last_seen_at from sessions order by id desc limit 5;"

# 보드가 뭘 보고하는지
... -c "select kind, message, count(*) from events where received_at > now() - interval '10 min' group by 1,2 order by 3 desc;"

# MQTT 접속 로그
ssh ... 'tail -30 /srv/monolith/log/server.log'

# 보드 패킷이 실제로 오는지
ssh ... 'sudo timeout 20 tcpdump -nn -i ens3 "tcp port 443 and net 211.234.0.0/16"'

# 인증서 체인 (subject와 issuer를 모두 본다)
echo | openssl s_client -connect 168.110.48.114.sslip.io:443 \
  -servername 168.110.48.114.sslip.io -showcerts 2>/dev/null > /tmp/c.txt
awk '/-----BEGIN CERT/,/-----END CERT/' /tmp/c.txt > /tmp/c.pem
csplit -sz -f /tmp/cc -b '%02d.pem' /tmp/c.pem '/-----BEGIN CERTIFICATE-----/' '{*}'
for f in /tmp/cc*.pem; do openssl x509 -in $f -noout -subject -issuer; done

# 백업 수동 실행
ssh ... 'sudo systemctl start monolith-backup.service && journalctl -u monolith-backup -n 30'
```

---

## 관련 문서

- `ORACLE_MONOLITH_LOGGER.md` — 설계 매뉴얼 (이 문서의 근거)
- `shared/apps_diag_can.h` — 진단 CAN 레이아웃 기준 정의
- `shared/apps_diag_can.dbc` — analyzer / decoder 설정용
- `stm32l432kc/DMA_SETUP_CHECKLIST.md` — L432 DMA 설정
- `TEM_45CH_DATA_PIPELINE_CHANGE.md` — Orion TEM 서버 배포·롤백·검증 기록
