# palworld-dedicatedserver-arm64

Apple Silicon과 ARM64 Linux에서 x86_64용 Palworld Dedicated Server를
Box64로 실행하는 Docker 구성입니다. 서버 프로그램과 설정, 월드 및 플레이어
데이터는 호스트 디렉터리에 보관하므로 컨테이너를 새로 만들어도 그대로 유지됩니다.

## 저장소 구성

```text
.
├── base/
│   ├── Dockerfile                  # ARM64용 Box64 베이스 이미지
│   └── patches/
│       └── box64-pal-clock.patch   # PalServer 시간 역행 방지 패치
├── installer/
│   └── Dockerfile                  # ARM64용 DepotDownloader
├── palworld/
│   ├── Dockerfile                  # Palworld 실행 이미지
│   ├── entrypoint-palworld.sh      # 서버 설정과 시간 기준을 준비하는 시작 스크립트
│   ├── pal-clock.c                 # preload 방식 비교·비상 전환용 라이브러리
│   ├── pal-clock-anchor.c          # NTP 기준 시각을 가져오는 ARM64 도구
│   └── pal-clock-probe.c           # 시간 함수와 syscall 경로 검증 도구
├── secrets/
│   └── .gitkeep
├── compose.yml
├── Makefile
└── .env.sample
```

Box64 베이스는 `corekeeper-dedicatedserver-arm64`와 같은 구조를 사용하며
Core Keeper 실행에 필요한 Box32도 함께 빌드합니다. Palworld용 시간 보정은
`BOX64_PAL_CLOCK=1`일 때만 작동하므로 다른 프로그램에는 적용되지 않습니다.

재현 가능한 빌드를 위해 Box64 v0.4.2의 다음 커밋으로 고정했습니다.

```text
7eeb5016493dab4e143d53da50dd47bfb44a9509
```

`.env.sample`에는 실제 검증에 사용한 이미지 태그를 넣었습니다. 다른 게임이 같은
Box64 베이스를 사용한다면 `latest`를 덮어쓰지 말고 게임별로 검증한 태그를
사용하는 편이 안전합니다.

## 요구 사항과 제한 사항

- Apple Silicon 또는 ARM64 Linux
- Linux ARM64 컨테이너를 실행할 수 있는 Docker
- Docker Desktop 메모리 16GB 이상 권장
- Palworld 컨테이너 메모리 제한 14GiB
- 게임 접속용 `8211/udp` 포트

Palworld Dedicated Server는 ARM64용 실행 파일을 제공하지 않으므로 Box64를 통해
x86_64 실행 파일을 구동합니다. 네이티브 x86_64 서버와 완전히 같은 안정성을
보장할 수 없으며, Palworld 또는 Box64가 업데이트되면 다시 검증해야 합니다.

## 준비

환경 변수 샘플을 복사합니다.

```bash
cp .env.sample .env
```

`PALWORLD_DATA_ROOT`는 서버 파일과 세이브를 저장할 호스트 경로입니다. 기본값을
그대로 쓰지 않는다면 `.env`에서 현재 호스트에 맞는 절대 경로로 바꾸십시오.

관리자 비밀번호는 `secrets/palworld_admin_password`에 저장합니다. 사용할 수 있는
문자는 영문, 숫자, `_`, `-`이며 길이는 16~64자입니다. `.env`와 `secrets/*`는
Git에 포함되지 않습니다.

## 빌드와 서버 설치

```bash
make build-base
docker compose --profile maintenance build palworld installer
make install
```

`make install`은 Palworld Dedicated Server (App ID `2394010`)를
`PALWORLD_DATA_ROOT`에 내려받습니다. Apple Silicon에서 32비트 SteamCMD를
에뮬레이션하지 않아도 되도록 ARM64용 DepotDownloader를 사용합니다.

## 서버 실행

```bash
make up-palworld
make status
make logs
```

- `make up-palworld`: Palworld 서버만 시작
- `make restart`: Palworld 서버 재시작
- `make down`: Compose에 포함된 컨테이너와 네트워크 종료

## 데이터와 서버 설정

호스트의 `PALWORLD_DATA_ROOT`는 컨테이너의 `/home/steam/palworld`에 연결됩니다.
서버 프로그램, 설정 파일, 월드와 플레이어 세이브가 모두 이 경로에 저장됩니다.

주요 설정 파일은 다음 위치에 있습니다.

```text
${PALWORLD_DATA_ROOT}/Pal/Saved/Config/LinuxServer/PalWorldSettings.ini
```

Palworld의 일반적인 운영 방식과 마찬가지로 호스트에서 이 파일을 수정한 다음
서버를 재시작하면 설정이 적용됩니다. 시작 스크립트는 내부 관리용 REST API를
사용하기 위해 다음 값만 자동으로 맞춥니다.

- `AdminPassword`: Docker secret에 저장된 관리자 비밀번호
- `RESTAPIEnabled=True`
- `RESTAPIPort=8212`

REST API 포트는 호스트에 공개하지 않고 Compose 내부 네트워크에서만 사용합니다.

## Negative delta time 문제와 대응 방식

Apple Silicon의 Docker Desktop에서 PalServer가 `Detected negative delta time`을
기록한 뒤 종료되는 현상이 확인됐습니다. 이 구성은 성능보다 시간 계산과 메모리
처리의 정확성을 우선하도록 Box64 기본값을 조정합니다.

```dotenv
BOX64_DYNAREC_BIGBLOCK=0
BOX64_DYNAREC_SAFEFLAGS=2
BOX64_DYNAREC_STRONGMEM=3
BOX64_DYNAREC_FASTROUND=0
BOX64_DYNAREC_FASTNAN=0
BOX64_DYNAREC_X87DOUBLE=1
BOX64_SYNC_ROUNDING=1
BOX64_RDTSC_1GHZ=1
```

`BOX64_RDTSC_1GHZ=1`은 Box64가 RDTSC를 1GHz 기준으로 에뮬레이션하도록 합니다.
나머지 값도 부동소수점과 메모리 처리의 정확성을 높이는 대신 일부 성능을
양보하도록 설정했습니다. 따라서 성능 위주 설정보다 CPU 사용량이 늘거나 서버
처리 FPS가 낮아질 수 있습니다.

이 설정만으로 모든 시간 역행을 막을 수 있는 것은 아닙니다. 서버 처리 FPS와
프레임 시간, 메모리 사용량, Docker 재시작 횟수, negative delta 로그를 함께
관찰해야 합니다.

### 최종 시간 보정 방식

PalServer가 Docker Desktop의 시스템 시각 조정을 직접 따라가다가 시간이 뒤로
움직이지 않도록 Box64에 Palworld 전용 시계를 추가했습니다.

서버가 시작될 때 여러 NTP 서버에서 현재 시각을 확인하고, 시스템 시각 조정과
무관하게 앞으로 흐르는 `CLOCK_MONOTONIC_RAW` 값도 함께 기록합니다. 이후
PalServer가 읽는 실시간은 `NTP 기준 시각 + 단조 시계의 경과 시간`으로
계산합니다. 호스트 시각이 나중에 뒤로 조정되더라도 PalServer에 전달되는
실시간은 뒤로 가지 않습니다.

다음 경로는 모두 하나의 시계 상태를 공유합니다.

- libc와 librt의 `clock_gettime`, `gettimeofday`, `time` 함수
- libc의 `syscall()` 함수
- x86 `syscall` 명령어
- Box64의 기존 vsyscall 연결 경로

`CLOCK_REALTIME` 이외의 시계 ID는 기존 Box64 동작을 그대로 따릅니다. RDTSC는
위의 `BOX64_RDTSC_1GHZ` 설정으로 별도 처리합니다.

```dotenv
PAL_CLOCK_MODE=box64
PAL_CLOCK_NTP_REQUIRED=0
PAL_CLOCK_NTP_SERVERS=time.cloudflare.com time.google.com pool.ntp.org
```

`PAL_CLOCK_MODE`는 다음 세 값을 지원합니다.

| 값 | 동작 | 용도 |
|---|---|---|
| `box64` | Box64 내부에서 함수와 syscall 경로를 함께 보정 | 기본값 |
| `preload` | `libpal-clock.so`로 표준 시간 함수만 가로챔 | 비교 시험 또는 임시 비상 전환 |
| `off` | Palworld 전용 시간 보정을 사용하지 않음 | 문제 분석 |

`box64` 모드에서 `libpal-clock.so`를 동시에 preload하면 보정이 두 번 적용될 수
있으므로 시작 스크립트가 이를 오류로 처리합니다.

### 검증 결과

기준 시각을 실제 시스템 시각보다 2초 느리게 설정한 뒤, 함수 호출과 두 syscall
경로를 번갈아 호출했습니다.

| 시험 | preload 방식 | Box64 방식 |
|---|---:|---:|
| 10만 회 호출 중 시간이 뒤로 간 횟수 | 100,000회 | 0회 |
| 100만 회 혼합 호출 실행 시간 | 0.856초 | 0.288초 |

이 결과는 현재 Apple Silicon Docker 환경에서 두 방식을 상대 비교한 값입니다.
모든 Palworld 업데이트와 장시간 운영의 안정성을 보장하는 수치는 아닙니다.

### NTP 연결에 실패했을 때

`PAL_CLOCK_NTP_REQUIRED=0`이 기본값입니다. 모든 NTP 서버에 연결하지 못하면 서버
시작 시점의 시스템 시각을 한 번만 기준값으로 사용하고, 그 뒤에는 단조 시계의
경과 시간만 더합니다. NTP 연결 없이는 서버를 시작하지 않으려면 다음과 같이
설정합니다.

```dotenv
PAL_CLOCK_NTP_REQUIRED=1
```

NTP 기준 시각은 PalServer를 시작할 때 한 번만 가져옵니다. 이 기능은 시간이 뒤로
가는 현상을 막지만, 실행 중에 NTP와 계속 동기화하거나 장기적인 시계 오차를
교정하지는 않습니다.

## 업데이트 전 확인 사항

- Box64 패치는 위에 적힌 커밋을 기준으로 작성했습니다. `BOX64_COMMIT`을 바꾼 뒤
  `git apply --check`가 실패한다면 검사를 제거하지 말고 새 Box64 소스에 맞게
  패치를 수정해야 합니다.
- Palworld 또는 Box64를 업데이트한 뒤에는 10만 회 엄격 모드 시간 경로 시험과
  별도 PalServer 시험 실행을 다시 수행하십시오.
- 실제 운영에서는 서버 처리 FPS, 프레임 시간, 메모리 사용량, 재시작 횟수와
  negative delta 로그를 장시간 확인하십시오.
- Core Keeper가 같은 Box64 베이스를 사용한다면 이미지 태그와 고유 식별값이
  의도치 않게 바뀌지 않았는지 함께 확인하십시오.

## 안전한 이미지 교체와 롤백

운영 이미지를 교체할 때는 다음 순서를 권장합니다.

1. Palworld REST API로 월드를 저장합니다.
2. Shutdown API의 `waittime=30`과 안내 문구로 접속자에게 30초 전에 알립니다.
3. 기존 서버의 종료 코드가 0이고 OOM으로 종료된 것이 아닌지 확인합니다.
4. 새 이미지를 시작합니다.
5. Docker 상태 검사, REST 상태 지표, 접속 상태와 최근 오류 로그를 확인합니다.

같은 월드와 `8211/udp` 포트를 두 서버가 동시에 사용할 수 없으므로 일반적인
블루/그린(blue/green) 방식으로 두 운영 서버를 함께 실행할 수는 없습니다. 별도
포트와 복제한 월드로 후보 이미지를 먼저 시험한 다음, 위 순서로 짧게 전환하는
방식이 안전합니다.

롤백할 가능성이 있다면 교체 전에 기존 이미지에 별도 태그를 붙여 보관하십시오.
롤백할 때는 `.env`의 `PALWORLD_IMAGE`를 이전 이미지 태그로 바꾸고 Palworld
컨테이너를 다시 만듭니다. `PAL_CLOCK_MODE=preload`는 현재 이미지 안에서 시간
보정 방식만 임시로 바꾸는 기능이며, 이전 이미지로 되돌리는 것과는 다릅니다.

## Watchpal 사용

Compose에는 선택적으로 Watchpal을 함께 실행할 수 있는 설정이 들어 있습니다.
Watchpal은 Palworld의 메모리 사용량을 감시하고, 주기적으로 월드를 저장하며,
필요할 때 접속자에게 미리 알린 뒤 안전하게 서버를 재시작합니다. 상태는 Discord
Webhook으로 전달할 수 있습니다.

Watchpal 소스는 이 저장소에 포함되지 않습니다. 기본 빌드 경로는
`../watchdog/container`이므로 해당 저장소가 없다면 `make up-palworld`로 게임
서버만 실행하십시오.

Watchpal은 공인 IP를 자동으로 조회해 Discord 상태 메시지에
`공인IP:8211 (UDP)` 형식으로 표시할 수 있습니다. 고정 IP나 도메인을 표시하려면
`.env`의 `WATCHPAL_PUBLIC_ADDRESS`를 설정합니다. 공인 IP가 Discord 채널에
노출되므로 채널의 공개 범위를 확인하십시오.

## 현재까지 확인한 항목

- Apple Silicon에서 ARM64 이미지 빌드와 PalServer 실행
- Box64 v0.4.2를 통한 Palworld Dedicated Server 실행
- `8211/udp` 접속과 컨테이너 상태 검사
- 실제 게임 클라이언트 접속과 다인 플레이
- REST API의 Metrics, Save, Announce, Shutdown 호출
- 함수 래퍼, libc `syscall()`, x86 `syscall` 명령어의 공통 시간 보정
- 2초의 강제 시각 차이에서 10만 회 호출 중 시간 역행 0회
- 호스트에 저장한 월드의 재시작 후 복원과 새 월드 생성
- Docker Desktop 16GB 환경에서 Palworld 컨테이너 14GiB 제한 적용
- Watchpal의 주기 저장, Discord 상태 갱신과 30초 사전 안내 후 재시작

장시간 다인 플레이, Palworld 업데이트 직후의 호환성, 다양한 ARM64 CPU에서의
성능과 안정성은 각 운영 환경에서 별도로 확인해야 합니다.
