# palworld-dedicatedserver-arm64

Apple Silicon과 ARM64 Linux에서 x86_64용 Palworld Dedicated Server를
Box64로 실행하는 Docker 구성입니다. 서버 파일과 세이브는 호스트에 보관되므로
컨테이너를 다시 만들어도 유지됩니다.

## 요구 사항

- Apple Silicon 또는 ARM64 Linux
- Linux ARM64 컨테이너를 실행할 수 있는 Docker
- Docker Desktop 메모리 16GB 이상 권장
- 게임 접속용 `8211/udp` 포트

Palworld 컨테이너에는 기본적으로 14GiB 메모리 제한이 적용됩니다. 이 구성은
ARM64 환경에서 x86_64 서버를 변환 실행하므로 Palworld나 Box64를 업데이트한
뒤에는 동작을 다시 확인해야 합니다.

## 빠른 시작

### 1. 환경 설정

```bash
cp .env.sample .env
```

`.env`의 `PALWORLD_DATA_ROOT`를 서버 파일과 세이브를 저장할 호스트의 절대
경로로 바꿉니다.

관리자 비밀번호는 `secrets/palworld_admin_password`에 저장합니다. 비밀번호는
영문, 숫자, `_`, `-`를 사용해 16~64자로 작성합니다. `.env`와 비밀번호 파일은
Git에 포함되지 않습니다.

Discord Webhook을 사용할 때는 URL을 `.env`에 직접 넣는 방식보다
`secrets/watchpal_webhook_url` 같은 파일을 만들고
`WATCHPAL_WEBHOOK_SECRET_FILE`로 지정하는 방식을 권장합니다. 그러면 Webhook
URL이 컨테이너 환경 변수에 노출되지 않습니다.

### 2. 이미지 빌드와 서버 설치

```bash
make build-base
docker compose --profile maintenance build palworld installer
make install
```

`make install`은 Palworld Dedicated Server(App ID `2394010`)를
`PALWORLD_DATA_ROOT`에 설치합니다.

### 3. 서버 실행

```bash
make up-palworld
make status
make logs
```

게임 클라이언트는 서버 호스트의 `8211/udp` 포트로 접속합니다. 포트를 바꾸려면
`.env`의 `PALWORLD_PORT`를 수정합니다.

## 자주 쓰는 명령

| 명령 | 설명 |
|---|---|
| `make up-palworld` | Palworld 서버만 시작 |
| `make restart` | Palworld 서버 재시작 |
| `make status` | 컨테이너 상태 확인 |
| `make logs` | Palworld 로그 확인 |
| `make down` | 컨테이너와 Compose 네트워크 종료 |

## 데이터와 서버 설정

`PALWORLD_DATA_ROOT`는 컨테이너의 `/home/steam/palworld`에 연결됩니다. 서버
프로그램, 설정, 월드와 플레이어 세이브가 모두 이 경로에 저장됩니다.

주요 설정 파일은 다음과 같습니다.

```text
${PALWORLD_DATA_ROOT}/Pal/Saved/Config/LinuxServer/PalWorldSettings.ini
```

이 파일을 수정한 뒤 서버를 재시작하면 설정이 적용됩니다. 시작 스크립트는 내부
관리용 REST API와 관련된 다음 값을 자동으로 설정합니다.

- `AdminPassword`: Docker secret에 저장한 관리자 비밀번호
- `RESTAPIEnabled=True`
- `RESTAPIPort=8212`

REST API 포트는 호스트에 공개되지 않으며 Compose 내부에서만 사용합니다.
RCON은 보안상 기본값인 `PALWORLD_RCON_ENABLED=0`으로 명시적으로 비활성화하며,
호스트 포트에도 게시하지 않습니다.

## 빠른 장애 복구와 진단

Watchpal은 Docker 프로세스 검사와 별도로 내부 REST `metrics`를 확인합니다.
기본 REST 제한 시간은 5초이고 2회 연속 실패 시 장애 상태로 표시하며 3회 연속
실패 시 같은 Palworld 컨테이너를 재기동합니다. 응답하지 않는 프로세스에 대한
Docker 종료 유예는 5초입니다. 반면 메모리 보호·수동 점검처럼 REST가 정상인
재기동에서는 기존처럼 저장 후 접속자에게 30초 전에 안내합니다.

이 구성은 UDP 라우터나 블루/그린 서버를 사용하지 않습니다. 같은 월드와 같은
게임 포트를 사용하는 컨테이너 하나를 빠르게 복구하므로, PalServer가 다시
준비되는 동안의 짧은 접속 중단은 남습니다.

PalServer의 stdout/stderr는 Crashpad가 참조하는 다음 파일에도 복제됩니다.

```text
${PALWORLD_DATA_ROOT}/Pal/Saved/Logs/Pal.log
```

기본 64MiB에 도달하면 시작 시 `Pal.log.previous`로 한 번 회전합니다. Box64는
`BOX64_SHOWBT=1`, `BOX64_SHOWSEGV=1`로 신호 시점의 네이티브·에뮬레이션
백트레이스를 남깁니다. 전체 호출 로그와 롤링 로그는 다중 스레드 서버의 실행을
교란하지 않도록 `BOX64_LOG=0`, `BOX64_ROLLING_LOG=0`으로 유지합니다. 실행 추적
전체를 켜는 `BOX64_DYNAREC_TRACE`나 디버거를 기다리는 `BOX64_JITGDB`도 서버
지연·정지를 유발할 수 있어 사용하지 않습니다. 각 옵션의 의미는
[Box64 공식 사용 문서](https://github.com/ptitSeb/box64/blob/main/docs/USAGE.md)를
기준으로 합니다.

실제 전환 검증에서 `BOX64_LOG=1`, `BOX64_ROLLING_LOG=64` 조합은 이미지와
무관하게 2~3분 안에 Box64 로그 포맷팅과 `clock_gettime` 경로가 겹친 SIGSEGV를
반복했습니다. 장애 증거를 더 많이 남기려는 설정이 서버 자체를 교란할 수 있으므로
운영 환경에서는 두 값을 다시 활성화하지 마십시오.

REST 실패 시점의 Docker 상태, 최근 로그, `Pal.log`, Crashpad 미니덤프는 기본
`./diagnostics`에 보관합니다. 서버가 다시 준비되기 전에는 작은 상태와 로그만
기록하고, `Pal/.sentry-native/pending/*.dmp` 복사와 SHA-256 계산은 복구 후에
진행하므로 덤프 처리가 재기동을 지연시키지 않습니다. 기본 보존 개수는 5개,
번들당 장애 산출물 상한은 256MiB입니다. 덤프와 로그에는 플레이어 ID·IP가
포함될 수 있으므로 이 디렉터리는 공개 저장소나 Discord에 올리지 않습니다.

## 라이브 월드 데이터 API 상태

Palworld 1.0 REST 문서에는 월드 액터 스냅샷을 반환하는
[`GET /game-data`](https://docs.palworldgame.com/api/rest-api/game-data/)가
정의되어 있습니다. 그러나 현재 검증한 서버 `v1.0.1.100619`에서는
`PalGameDataBridge GameData API is not enabled`와 함께 404를 반환했고, 공식
설정 문서에는 이를 활성화하는 항목이 없습니다.

격리 서버에서 RCON `EnableGameDataAPI`, 실행 인자 `-EnableGameDataAPI`, Unreal
`-ExecCmds=EnableGameDataAPI`도 각각 활성화되지 않는 것을 확인했습니다. 따라서
현재 Compose에는 검증되지 않은 디버그 옵션이나 바이너리 패치를 넣지 않았습니다.
공식 활성화 방법이 공개되기 전까지는 기존 `metrics`, `players`, `settings`
엔드포인트만 운용 데이터로 사용합니다. Palworld 공식 문서 역시 REST/RCON을
인터넷에 직접 공개하지 말고 LAN에서만 쓰도록 경고하며, 현재 구성은 REST와
RCON 포트를 호스트에 게시하지 않습니다.

## ARM64 안정성 설정

Apple Silicon의 Docker Desktop에서는 PalServer가 `Detected negative delta time`을
기록하고 종료되는 문제가 확인됐습니다. 이 저장소는 이를 줄이기 위해 검증한
Box64 커밋과 안정성 설정을 `.env.sample`에 고정해 둡니다.

기본값인 `PAL_CLOCK_MODE=box64`는 NTP 기준 시각과 단조 시계를 조합해 PalServer에
전달되는 시간이 뒤로 가지 않도록 합니다. 모든 스레드는 마지막 시각을 별도로
저장하지 않고 동일한 `CLOCK_MONOTONIC_RAW` 경과 시간을 사용합니다. 따라서
Docker VM의 실시간 시계 조정은 게임 서버 시간에 반영되지 않으며, 같은 시각이
연속으로 관측되더라도 값을 인위적으로 전진시키지 않습니다.

NTP 서버에 연결하지 못하면 기본값인 `PAL_CLOCK_NTP_REQUIRED=0`에 따라 시작
시점의 시스템 시각을 기준으로 계속 실행합니다. NTP 연결 실패 시 서버를
시작하지 않으려면 이 값을 `1`로 바꿉니다.

## 업데이트와 롤백

Palworld 또는 Box64를 업데이트하기 전에는 월드를 저장하고 접속자에게 종료를
안내합니다. 업데이트 후에는 컨테이너 상태, 접속 여부, 최근 오류 로그와
`negative delta time` 발생 여부를 확인합니다.

롤백에 대비하려면 기존 이미지 태그를 보관합니다. 문제가 생기면 `.env`의
`PALWORLD_IMAGE`를 이전 태그로 되돌린 뒤 컨테이너를 다시 시작합니다. 같은 월드와
`8211/udp` 포트를 두 서버가 동시에 사용할 수 없으므로 운영 서버 두 개를 동시에
띄우는 방식의 교체는 지원하지 않습니다.

## 보안 기본값

- Palworld와 Watchpal 모두 모든 Linux capability를 제거하고
  `no-new-privileges`를 사용합니다.
- Palworld는 기존처럼 비루트 `steam` 사용자로 실행합니다.
- Watchpal의 루트 파일시스템은 읽기 전용이며 `/tmp`, 상태 볼륨, 진단 경로만
  쓰기 가능합니다.
- 관리자 비밀번호는 Docker secret으로만 전달하고 REST/RCON은 호스트에 공개하지
  않습니다.
- Watchpal의 Docker socket 마운트는 대상 컨테이너 재기동에 필요하지만 Docker
  데몬 제어 권한과 동등한 고권한 경계입니다. 신뢰할 수 있는 로컬 이미지 외에는
  이 마운트를 공유하지 않습니다.

## 확인된 범위

- Apple Silicon에서 ARM64 이미지 빌드와 PalServer 실행
- 실제 게임 클라이언트 접속과 다인 플레이
- `8211/udp` 접속과 REST API의 저장·공지·종료 호출
- 읽기 전용 Watchpal과 capability 제거 상태에서 Docker 상태·미니덤프 수집
- 격리 SIGSEGV 시험에서 0.25초 안에 미니덤프 출현, 1.25초 이내 크기 안정화
- Box64 네이티브·에뮬레이션 백트레이스 생성
- `Pal.log` 생성·Crashpad 첨부 및 99MiB 미니덤프 진단 번들 복사
- 현재 바이너리의 `/game-data` 404와 비공개 활성화 후보 3종 실패 확인
- 약 502만 회의 시간 함수 호출과 약 377만 회의 delta 비교에서 음수 delta 0건
- 0 delta 354건을 허용한 상태에서 시간 함수와 syscall 경로 테스트 통과
- v3 운영 이미지를 접속자 없이 20분 55초 관찰해 41개 표본 모두 `healthy`,
  재시작·OOM·delta 관련 오류 0건 확인
- 전체·롤링 로그를 끈 진단 v2 이미지를 복제 월드에서 8분간 48회, 운영 월드에서
  6분간 36회 확인해 REST 200, 재시작 0, OOM 0을 확인
- 컨테이너 재시작 후 호스트에 저장한 월드 복원

장시간 운영, Palworld 업데이트 직후의 호환성, 다른 ARM64 CPU에서의 성능과
안정성, 장시간 실제 플레이 부하에서의 진단 v2 동작은 각 환경에서 별도로
확인해야 합니다.
