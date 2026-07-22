#!/bin/bash
set -Eeuo pipefail
umask 077

palworld_dir="${PALWORLD_DIR:-/home/steam/palworld}"
server_binary="${palworld_dir}/Pal/Binaries/Linux/PalServer-Linux-Shipping"
settings_file="${palworld_dir}/Pal/Saved/Config/LinuxServer/PalWorldSettings.ini"
default_settings_file="${palworld_dir}/DefaultPalWorldSettings.ini"
admin_password_file="${PALWORLD_ADMIN_PASSWORD_FILE:-}"

if [[ ! -f "$server_binary" ]]; then
    echo "[epko-palworld] 서버 바이너리를 찾을 수 없습니다: $server_binary" >&2
    echo "[epko-palworld] 먼저 'make install'로 App ID 2394010을 설치하세요." >&2
    exit 1
fi

if [[ -n "$admin_password_file" ]]; then
    if [[ ! -r "$admin_password_file" ]]; then
        echo "[epko-palworld] 관리자 비밀번호 secret을 읽을 수 없습니다: $admin_password_file" >&2
        exit 1
    fi
    admin_password="$(tr -d '\r\n' < "$admin_password_file")"
    if [[ ! "$admin_password" =~ ^[A-Za-z0-9_-]{16,64}$ ]]; then
        echo "[epko-palworld] 관리자 비밀번호는 16~64자의 영문, 숫자, _, -만 사용할 수 있습니다." >&2
        exit 1
    fi

    mkdir -p "$(dirname "$settings_file")"
    if [[ ! -s "$settings_file" ]] || ! grep -q '[^[:space:]]' "$settings_file"; then
        cp "$default_settings_file" "$settings_file"
    fi

    if ! grep -q 'AdminPassword=' "$settings_file" \
        || ! grep -q 'RESTAPIEnabled=' "$settings_file" \
        || ! grep -q 'RESTAPIPort=' "$settings_file"; then
        echo "[epko-palworld] PalWorldSettings.ini에 REST API 설정 키가 없습니다." >&2
        exit 1
    fi

    sed -i \
        -e "s/AdminPassword=\"[^\"]*\"/AdminPassword=\"${admin_password}\"/" \
        -e 's/RESTAPIEnabled=False/RESTAPIEnabled=True/' \
        -e 's/RESTAPIPort=[0-9][0-9]*/RESTAPIPort=8212/' \
        "$settings_file"
    echo "[epko-palworld] 내부 REST API 활성화: 8212/tcp"

    rcon_enabled="${PALWORLD_RCON_ENABLED:-0}"
    rcon_port="${PALWORLD_RCON_PORT:-25575}"
    if [[ ! "$rcon_port" =~ ^[0-9]+$ ]] || (( rcon_port < 1 || rcon_port > 65535 )); then
        echo "[epko-palworld] PALWORLD_RCON_PORT는 1~65535 범위여야 합니다." >&2
        exit 1
    fi
    if ! grep -q 'RCONEnabled=' "$settings_file" || ! grep -q 'RCONPort=' "$settings_file"; then
        echo "[epko-palworld] PalWorldSettings.ini에 RCON 설정 키가 없습니다." >&2
        exit 1
    fi
    case "$rcon_enabled" in
    1)
        sed -i \
            -e 's/RCONEnabled=False/RCONEnabled=True/' \
            -e "s/RCONPort=[0-9][0-9]*/RCONPort=${rcon_port}/" \
            "$settings_file"
        echo "[epko-palworld] 내부 진단용 RCON 활성화: ${rcon_port}/tcp"
        ;;
    0)
        sed -i 's/RCONEnabled=True/RCONEnabled=False/' "$settings_file"
        ;;
    *)
        echo "[epko-palworld] PALWORLD_RCON_ENABLED는 0 또는 1이어야 합니다." >&2
        exit 1
        ;;
    esac
fi

# DepotDownloader와 macOS bind mount 조합에서는 실행 비트가 보존되지 않을 수 있다.
chmod +x "$server_binary"
if [[ -f "${palworld_dir}/Pal/Plugins/Sentry/Binaries/Linux/crashpad_handler" ]]; then
    chmod +x "${palworld_dir}/Pal/Plugins/Sentry/Binaries/Linux/crashpad_handler"
fi

library_paths=(
    "${palworld_dir}/linux64"
    "${palworld_dir}/Pal/Binaries/Linux"
    "${palworld_dir}/Engine/Binaries/Linux"
)

joined_library_paths=""
for library_path in "${library_paths[@]}"; do
    if [[ -d "$library_path" ]]; then
        if [[ -n "$joined_library_paths" ]]; then
            joined_library_paths+=":"
        fi
        joined_library_paths+="$library_path"
    fi
done

if [[ -n "$joined_library_paths" ]]; then
    export LD_LIBRARY_PATH="${joined_library_paths}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
    export BOX64_LD_LIBRARY_PATH="${joined_library_paths}${BOX64_LD_LIBRARY_PATH:+:${BOX64_LD_LIBRARY_PATH}}"
fi

pal_clock_mode="${PAL_CLOCK_MODE:-}"
if [[ -z "$pal_clock_mode" ]]; then
    if [[ "${PAL_CLOCK_ENABLED:-1}" == "1" ]]; then
        pal_clock_mode="preload"
    else
        pal_clock_mode="off"
    fi
fi

case "$pal_clock_mode" in
preload|box64|off)
    ;;
*)
    echo "[epko-palworld] 잘못된 PAL_CLOCK_MODE입니다: $pal_clock_mode (preload, box64, off)" >&2
    exit 1
    ;;
esac

if [[ "$pal_clock_mode" != "off" ]]; then
    read -r -a ntp_servers <<< "${PAL_CLOCK_NTP_SERVERS:-time.cloudflare.com time.google.com pool.ntp.org}"
    if clock_anchor="$(pal-clock-anchor "${ntp_servers[@]}")"; then
        read -r PAL_CLOCK_ANCHOR_EPOCH_NS PAL_CLOCK_ANCHOR_MONOTONIC_NS <<< "$clock_anchor"
        export PAL_CLOCK_ANCHOR_EPOCH_NS PAL_CLOCK_ANCHOR_MONOTONIC_NS
        echo "[epko-palworld] Pal clock 기준시각: 외부 NTP (${#ntp_servers[@]}개 서버 조회)"
    elif [[ "${PAL_CLOCK_NTP_REQUIRED:-0}" == "1" ]]; then
        echo "[epko-palworld] 외부 NTP 기준시각을 가져오지 못해 기동을 중단합니다." >&2
        exit 1
    else
        clock_anchor="$(pal-clock-anchor --system)"
        read -r PAL_CLOCK_ANCHOR_EPOCH_NS PAL_CLOCK_ANCHOR_MONOTONIC_NS <<< "$clock_anchor"
        export PAL_CLOCK_ANCHOR_EPOCH_NS PAL_CLOCK_ANCHOR_MONOTONIC_NS
        echo "[epko-palworld] 경고: NTP 조회 실패, 시작 시 시스템 시각을 1회 기준값으로 사용합니다." >&2
    fi

    if [[ "$pal_clock_mode" == "preload" ]]; then
        pal_clock_library="/usr/local/lib/pal-clock/libpal-clock.so"
        export BOX64_PAL_CLOCK=0
        export BOX64_LD_PRELOAD="${pal_clock_library}${BOX64_LD_PRELOAD:+:${BOX64_LD_PRELOAD}}"
        echo "[epko-palworld] Pal clock 모드: preload"
    else
        if [[ "${BOX64_LD_PRELOAD:-}" == *libpal-clock.so* ]]; then
            echo "[epko-palworld] box64 모드와 libpal-clock.so preload를 동시에 사용할 수 없습니다." >&2
            exit 1
        fi
        export BOX64_PAL_CLOCK=1
        echo "[epko-palworld] Pal clock 모드: Box64 내부 공통 시계"
    fi
else
    export BOX64_PAL_CLOCK=0
fi

echo "[epko-palworld] Box64: $(box64 -v 2>&1 | head -n 1)"
echo "[epko-palworld] 실행 파일: $server_binary"

cd "$palworld_dir"
exec box64 "$server_binary" Pal "$@"
