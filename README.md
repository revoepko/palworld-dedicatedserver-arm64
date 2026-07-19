# palworld-dedicatedserver-arm64

ARM64 환경에서 Palworld Dedicated Server의 Linux x86_64 배포물을 Box64로
실행하기 위한 Docker 구성입니다. 서버 설치·설정·세이브는 호스트 디렉터리에
영속화합니다.

## 구성

```text
.
├── base/
│   ├── Dockerfile                  # ARM64용 Box64 베이스
│   └── patches/
│       └── box64-pal-clock.patch   # Box64 공통 가상시계 패치
├── installer/
│   └── Dockerfile                  # ARM64 DepotDownloader
├── palworld/
│   ├── Dockerfile                  # Palworld 런타임
│   ├── entrypoint-palworld.sh      # REST·시간 설정 보정 및 Box64 실행
│   ├── pal-clock.c                 # preload 비교·롤백용 가상 시계
│   ├── pal-clock-anchor.c          # 외부 NTP 기준시각 조회기
│   └── pal-clock-probe.c           # 시간 경로 혼합 검증 도구
├── secrets/
│   └── .gitkeep
├── compose.yml
├── Makefile
└── .env.sample
```

`base/`는 `corekeeper-dedicatedserver-arm64`에서 검증한 Box64 베이스와 같은
구성이며 Core Keeper의 Box32 요구사항도 유지합니다. Palworld 전용 시각 패치는
`BOX64_PAL_CLOCK=1`일 때만 활성화됩니다. 다만 운영 중인 다른 게임의 베이스가
의도치 않게 바뀌지 않도록 `.env.sample`은 `latest` 대신 검증한
`epko-base:pal-clock-v2` 태그를 사용합니다. 이미 해당 이미지가 있으면 베이스
빌드를 생략할 수 있습니다. 기본 Box64는 v0.4.2의
`7eeb5016493dab4e143d53da50dd47bfb44a9509` 커밋으로 고정되어 있습니다.
Palworld와 Watchpal 이미지도 최종 검증에 사용한 명시적 태그를 샘플에 고정합니다.

## 요구 사항과 제한

- Apple Silicon 또는 ARM64 Linux에서 Linux ARM64 컨테이너를 실행할 수 있는 Docker
- Docker Desktop 메모리 16GB 이상
- Palworld 서버용 Docker 하드 제한 14GiB
- 서버 포트 `8211/udp`

Palworld 서버는 공식 ARM64 바이너리를 제공하지 않으므로 Box64 변환 계층을
사용합니다. 네이티브 x86_64 환경과 동일한 안정성을 보장하지 않으며 게임
업데이트에 따라 추가 런타임 조정이 필요할 수 있습니다.

## 준비

```bash
cp .env.sample .env
```

`secrets/palworld_admin_password`에 16~64자의 영문, 숫자, `_`, `-`로 구성된
관리자 비밀번호를 저장합니다. `.env`와 `secrets/*`는 Git에서 제외됩니다.

## 빌드와 설치

```bash
make build-base
docker compose --profile maintenance build palworld installer
make install
```

`make install`은 Palworld Dedicated Server App ID `2394010`을
`PALWORLD_DATA_ROOT`에 설치합니다. SteamCMD 대신 ARM64에서 직접 실행할 수 있는
DepotDownloader를 사용합니다.

## 실행

```bash
make up-palworld
make status
make logs
```

- `make up-palworld`: 게임 서버만 실행
- `make restart`: 게임 서버 재시작
- `make down`: 관련 컨테이너와 네트워크 종료

## 데이터와 서버 설정

기본 호스트 데이터 경로는 `/Users/epko/steamcmd-data/palworld`이며 컨테이너의
`/home/steam/palworld`에 바인드 마운트됩니다. 서버 설치본, 설정, 세이브가 모두
이 경로에 남습니다.

주 설정 파일은 다음과 같습니다.

```text
/Users/epko/steamcmd-data/palworld/Pal/Saved/Config/LinuxServer/PalWorldSettings.ini
```

Palworld의 기본 운용 방식대로 호스트에서 이 파일을 수정하고 서버를 재시작해
적용합니다. 엔트리포인트는 내부 관리용 REST API를 위해 아래 세 항목만 시작 시
보정합니다.

- `AdminPassword`: Docker secret 값
- `RESTAPIEnabled=True`
- `RESTAPIPort=8212`

REST 포트는 호스트에 공개하지 않고 Compose 내부 네트워크에서만 사용합니다.

## ARM64 안정성 설정

Palworld 프로세스에서 `Detected negative delta time` 충돌이 확인되어 기본값은
Box64의 속도보다 시간 계산, 부동소수점 처리, 메모리 순서의 정확성을 우선합니다.
아래 값은 이미지와 Compose의 기본값이며 `.env`에서 각각 재정의할 수 있습니다.

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

`BOX64_RDTSC_1GHZ=1`은 1GHz보다 정밀도가 낮은 하드웨어 카운터 대신 Box64의
소프트웨어 카운터를 사용하게 합니다. 이 설정은 충돌 가능성을 줄이는 완화책이며
장시간 무중단을 보장하지는 않습니다.
보수적인 dynarec 설정이므로 기존 성능 우선값보다 CPU 사용량이 늘거나 서버 처리
FPS가 낮아질 수 있습니다. Watchpal 상태 메시지의 서버 처리 FPS와 프레임 시간,
Docker 재시작 횟수를 함께 확인해 안정성과 성능을 판단합니다.

### PalServer 전용 시각

PalServer가 Docker Desktop의 벽시계 보정을 직접 따라가다가 시간이 뒤로 움직이는
문제를 피하도록 Box64 내부에 공통 가상시계를 적용합니다. 시작 시 외부 NTP 서버
여러 곳에서 기준시각을 구하고, 이후 PalServer가 읽는 실시간은
`CLOCK_MONOTONIC_RAW` 경과분으로 계산하므로 호스트의 이후 시각 step에 역행하지
않습니다. libc·librt 시간 함수, libc `syscall()` 및 x86 `syscall` 명령어가 같은
시계 상태를 사용하며, Box64의 기존 vsyscall 브리지 역시 동일한 syscall 처리기로
연결됩니다.

```dotenv
PAL_CLOCK_MODE=box64
PAL_CLOCK_NTP_REQUIRED=0
PAL_CLOCK_NTP_SERVERS=time.cloudflare.com time.google.com pool.ntp.org
```

`PAL_CLOCK_MODE=preload`는 비교·긴급 롤백용 기존 방식이며 표준 시간 함수만
가로챕니다. `PAL_CLOCK_MODE=off`는 두 보정을 모두 끕니다. `box64` 모드와
`libpal-clock.so` preload는 이중 적용되지 않도록 엔트리포인트에서 차단합니다.

2초의 강제 시각 차이를 둔 혼합 경로 10만 회 시험에서 preload 모드는 10만 회
역행한 반면 Box64 모드는 0회였습니다. 같은 혼합 호출 100만 회에서는 각각
0.856초와 0.288초가 걸려 Box64 모드가 syscall 전환 비용도 줄였습니다. 이 수치는
현재 Apple Silicon Docker 환경의 상대 비교값입니다.

기본값에서는 모든 NTP 조회가 실패해도 서버 가용성을 유지하기 위해 프로세스 시작
시 시스템 시각을 한 번만 기준값으로 사용합니다. 이후에는 동일하게 monotonic
시계만 누적합니다. 외부 NTP 없이는 기동하지 않게 하려면
`PAL_CLOCK_NTP_REQUIRED=1`로 설정합니다.

### 운영 주의사항

- 가상시계 패치는 위에 적힌 Box64 커밋에 맞춰져 있습니다. `BOX64_COMMIT`을
  변경했을 때 `git apply --check`가 실패하면 검사를 제거하지 말고 새 소스에 맞게
  패치를 이식한 뒤 다시 빌드·검증해야 합니다.
- NTP 기준시각은 PalServer 프로세스가 시작할 때 한 번 결정됩니다. 공통 시계는
  그 시각부터 역행하지 않게 만들지만 지속적인 NTP 재동기화나 장기 드리프트
  보정까지 제공하지는 않습니다. 시작 시 시각 정확성이 가용성보다 중요하면
  `PAL_CLOCK_NTP_REQUIRED=1`을 사용합니다.
- `PAL_CLOCK_MODE=box64`에서는 `libpal-clock.so`를 `BOX64_LD_PRELOAD`에 직접
  추가하지 않습니다. 두 보정의 이중 적용은 엔트리포인트가 오류로 차단합니다.
- Palworld 또는 Box64를 업데이트한 뒤에는 강제 시각 차이를 둔 10만 회 strict
  혼합 경로 시험, 격리된 PalServer 후보 기동, 서버 처리 FPS·프레임 시간·재시작
  횟수·negative delta 로그의 장시간 관찰을 다시 수행합니다.
- Core Keeper와 베이스 이미지 이름을 공유하므로 운영에서는 `latest`를 덮어쓰기보다
  게임별로 검증한 명시적 태그를 사용합니다. 이 패치는 `BOX64_PAL_CLOCK=1`일 때만
  활성화되지만 Core Keeper가 참조하는 이미지 digest가 의도치 않게 바뀌지 않도록
  함께 확인해야 합니다.

### 안전한 교체와 롤백

운영 이미지를 교체할 때는 Palworld REST API로 먼저 저장하고, Shutdown API의
`waittime=30`과 안내 문구를 사용해 접속자에게 30초 전에 알립니다. 기존 서버가
exit code 0으로 종료되고 OOM 종료가 아님을 확인한 다음 새 이미지를 기동하며,
기동 후 healthcheck·REST metrics·Watchpal 상태를 모두 확인합니다. 같은 월드와
`8211/udp`를 두 서버가 동시에 소유할 수 없으므로 운영 전환에는 일반적인
blue/green 동시 기동을 사용하지 않습니다.

현재 호스트의 정확한 이전 이미지 롤백 태그는
`epko-palworld:rollback-pal-clock-v1`입니다. 이 태그는 로컬 Docker에만 존재할 수
있으므로 정리 전에 image ID를 확인해야 합니다. 정확한 이미지 롤백이 아니라 v2
이미지 안에서 시간 경로만 임시로 되돌리려면 `PAL_CLOCK_MODE=preload`를 사용할 수
있지만, raw syscall 경로를 공통 보정하지 못하므로 장기 운영 기본값으로 사용하지
않습니다. 어느 방식이든 위의 저장·30초 공지·정상 종료 절차를 먼저 수행합니다.

Watchpal은 기본적으로 공인 IP를 자동 조회해 Discord 상태 메시지에
`공인IP:8211 (UDP)` 형태의 외부 접속 주소를 표시합니다. 고정 IP나 도메인을
직접 표시하려면 `.env`의 `WATCHPAL_PUBLIC_ADDRESS`에 값을 지정합니다. 자동 조회
결과는 1시간 캐시되며 조회에 실패해도 포트 정보와 나머지 상태 갱신은 유지됩니다.
공인 IP가 Webhook 채널에 표시되므로 해당 Discord 채널의 공개 범위를 확인해야
합니다.

## 확인된 범위

- ARM64 이미지와 서버 설치 이미지 구성
- Box64를 통한 Palworld 서버 기동
- `8211/udp` 리슨과 컨테이너 healthcheck
- REST API의 metrics·Save·Shutdown 연동
- 외부 NTP 기준시각과 Box64 공통 시계를 통한 wrapper·syscall 시간 경로 보정
- 강제 2초 시각 차이에서 10만 회 혼합 경로 무역행 및 100만 회 성능 비교
- 호스트 세이브 영속화와 재기동 후 복원
- 16GB Docker Desktop 환경에서 14GiB 컨테이너 제한 적용

실제 게임 클라이언트 접속, 장시간 다인 플레이 안정성, 외부 포트 포워딩은
운영 환경에서 별도로 확인해야 합니다.
