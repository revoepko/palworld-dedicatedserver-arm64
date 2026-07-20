# syntax=docker/dockerfile:1

# 기존 Core Keeper용 epko-base에서 직접 빌드한 Box64 산출물을 재사용한다.
ARG BOX64_BASE_IMAGE=epko-base:latest
FROM ${BOX64_BASE_IMAGE} AS box64-base

FROM debian:trixie-slim AS clock-builder
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        gcc \
        gcc-x86-64-linux-gnu \
        libc6-dev \
        libc6-dev-amd64-cross \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY pal-clock.c pal-clock-anchor.c pal-clock-probe.c ./
RUN gcc -O2 -Wall -Wextra -Werror -o pal-clock-anchor pal-clock-anchor.c \
    && x86_64-linux-gnu-gcc -O2 -Wall -Wextra -Werror -fPIC -shared \
        -o libpal-clock.so pal-clock.c \
    && x86_64-linux-gnu-gcc -O2 -Wall -Wextra -Werror -pthread \
        -o pal-clock-probe pal-clock-probe.c

# Palworld 1.0의 SDL3 런타임을 위해 Debian trixie를 사용한다.
FROM debian:trixie-slim

ENV USER=steam \
    HOME=/home/steam \
    LANG=en_US.UTF-8 \
    PALWORLD_DIR=/home/steam/palworld \
    BOX64_DYNAREC_STRONGMEM=3 \
    BOX64_DYNAREC_BIGBLOCK=0 \
    BOX64_DYNAREC_SAFEFLAGS=2 \
    BOX64_DYNAREC_FASTROUND=0 \
    BOX64_DYNAREC_FASTNAN=0 \
    BOX64_DYNAREC_X87DOUBLE=1 \
    BOX64_SYNC_ROUNDING=1 \
    PAL_CLOCK_MODE=box64 \
    PAL_CLOCK_NTP_SERVERS="time.cloudflare.com time.google.com pool.ntp.org" \
    BOX64_LOG=0

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        libatomic1 \
        libcurl3t64-gnutls \
        libicu76 \
        libsdl3-0 \
        locales \
        procps \
        xdg-user-dirs \
    && localedef -i en_US -c -f UTF-8 \
        -A /usr/share/locale/locale.alias en_US.UTF-8 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --home-dir "$HOME" --shell /bin/bash "$USER" \
    && mkdir -p "$PALWORLD_DIR" \
    && chown -R "$USER:$USER" "$HOME"

COPY --from=box64-base /usr/local/bin/box64 /usr/local/bin/box64
COPY --from=box64-base /usr/lib/box64-x86_64-linux-gnu /usr/lib/box64-x86_64-linux-gnu
COPY --from=box64-base /etc/box64.box64rc /etc/box64.box64rc

COPY --from=clock-builder /src/pal-clock-anchor /usr/local/bin/pal-clock-anchor
COPY --from=clock-builder /src/libpal-clock.so /usr/local/lib/pal-clock/libpal-clock.so
COPY --from=clock-builder /src/pal-clock-probe /usr/local/lib/pal-clock/pal-clock-probe

COPY entrypoint-palworld.sh /usr/local/bin/entrypoint-palworld.sh
RUN chmod 755 /usr/local/bin/entrypoint-palworld.sh

USER $USER
WORKDIR $PALWORLD_DIR

ENTRYPOINT ["/usr/local/bin/entrypoint-palworld.sh"]
HEALTHCHECK --interval=30s --timeout=5s --start-period=5m --retries=3 \
    CMD pgrep -f PalServer-Linux-Shipping >/dev/null \
        && bash -c 'exec 3<>/dev/tcp/127.0.0.1/8212' \
        || exit 1
