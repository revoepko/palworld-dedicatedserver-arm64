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
관리용 REST API를 위해 다음 값만 자동으로 설정합니다.

- `AdminPassword`: Docker secret에 저장한 관리자 비밀번호
- `RESTAPIEnabled=True`
- `RESTAPIPort=8212`

REST API 포트는 호스트에 공개되지 않으며 Compose 내부에서만 사용합니다.

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

## 확인된 범위

- Apple Silicon에서 ARM64 이미지 빌드와 PalServer 실행
- 실제 게임 클라이언트 접속과 다인 플레이
- `8211/udp` 접속과 REST API의 저장·공지·종료 호출
- 약 502만 회의 시간 함수 호출과 약 377만 회의 delta 비교에서 음수 delta 0건
- 0 delta 354건을 허용한 상태에서 시간 함수와 syscall 경로 테스트 통과
- v3 운영 이미지를 접속자 없이 20분 55초 관찰해 41개 표본 모두 `healthy`,
  재시작·OOM·delta 관련 오류 0건 확인
- 컨테이너 재시작 후 호스트에 저장한 월드 복원

장시간 운영, Palworld 업데이트 직후의 호환성, 다른 ARM64 CPU에서의 성능과
안정성, 실제 플레이 부하에서의 v3 동작은 각 환경에서 별도로 확인해야 합니다.
