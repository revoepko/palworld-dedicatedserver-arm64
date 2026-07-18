#!/bin/bash
set -Eeuo pipefail

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

echo "[epko-palworld] Box64: $(box64 -v 2>&1 | head -n 1)"
echo "[epko-palworld] 실행 파일: $server_binary"

cd "$palworld_dir"
exec box64 "$server_binary" Pal "$@"
