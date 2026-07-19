# palworld-dedicatedserver-arm64

ARM64 환경에서 Palworld Dedicated Server의 Linux x86_64 배포물을 Box64로
실행하기 위한 Docker 구성입니다. 서버 설치·설정·세이브는 호스트 디렉터리에
영속화합니다.

## 구성

```text
.
├── base/
│   └── Dockerfile                  # ARM64용 Box64 베이스
├── installer/
│   └── Dockerfile                  # ARM64 DepotDownloader
├── palworld/
│   ├── Dockerfile                  # Palworld 런타임
│   └── entrypoint-palworld.sh      # REST 설정 보정 및 Box64 실행
├── secrets/
│   └── .gitkeep
├── compose.yml
├── Makefile
└── .env.sample
```

`base/`는 `corekeeper-dedicatedserver-arm64`에서 검증한 Box64 베이스와 같은
구성입니다. 두 저장소가 같은 `epko-base:latest`를 사용해도 Core Keeper의
Box32 요구사항을 유지합니다. 이미 해당 이미지가 있으면 베이스 빌드를 생략할
수 있습니다. 기본 Box64 커밋은 두 게임의 기동을 확인한 `24065fffb`로
고정되어 있습니다.

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
```

이 설정은 충돌 가능성을 줄이는 완화책이며 장시간 무중단을 보장하지는 않습니다.
보수적인 dynarec 설정이므로 기존 성능 우선값보다 CPU 사용량이 늘거나 서버 처리
FPS가 낮아질 수 있습니다. Watchpal 상태 메시지의 서버 처리 FPS와 프레임 시간,
Docker 재시작 횟수를 함께 확인해 안정성과 성능을 판단합니다.

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
- 호스트 세이브 영속화와 재기동 후 복원
- 16GB Docker Desktop 환경에서 14GiB 컨테이너 제한 적용

실제 게임 클라이언트 접속, 장시간 다인 플레이 안정성, 외부 포트 포워딩은
운영 환경에서 별도로 확인해야 합니다.
