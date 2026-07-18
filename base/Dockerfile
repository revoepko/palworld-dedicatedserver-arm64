# Stage 1: 32비트 라이브러리 추출
FROM --platform=linux/386 i386/debian:bookworm-slim AS libs-source
RUN apt-get update && apt-get install -y --no-install-recommends \
    libc6 libstdc++6 libgcc-s1 libcurl4 libncurses5 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Stage 2: Box64 빌드
FROM debian:bookworm-slim AS builder
ARG BOX64_COMMIT=24065fffb
RUN apt-get update && apt-get install -y git build-essential cmake python3
WORKDIR /src
RUN git clone https://github.com/ptitSeb/box64.git \
    && git -C /src/box64 checkout "$BOX64_COMMIT"
WORKDIR /src/box64/build
RUN cmake .. -DARM_DYNAREC=ON -DBOX32=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    && make -j$(nproc) \
    && make install DESTDIR=/tmp/install

# Stage 3: Core Keeper와 공유하는 최종 베이스 이미지
FROM debian:bookworm-slim
ENV USER=steam HOME=/home/steam

RUN apt-get update && apt-get install -y \
    curl ca-certificates locales libatomic1 \
    xvfb libxi6 libxrender1 libxtst6 libxext6 \
    libdbus-1-3 libxcursor1 libxinerama1 libxss1 \
    x11-utils \
    && localedef -i en_US -c -f UTF-8 -A /usr/share/locale/locale.alias en_US.UTF-8 \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /tmp/.X11-unix && chmod 1777 /tmp/.X11-unix
RUN useradd -m -d $HOME -s /bin/bash $USER

COPY --from=builder /tmp/install/usr/local/bin/box64 /usr/local/bin/box64
COPY --from=builder /tmp/install/usr/lib/box64-x86_64-linux-gnu /usr/lib/box64-x86_64-linux-gnu
COPY --from=builder /tmp/install/usr/lib/box64-i386-linux-gnu /usr/lib/box64-i386-linux-gnu
COPY --from=builder /tmp/install/etc/box64.box64rc /etc/box64.box64rc

COPY --from=libs-source /lib/i386-linux-gnu /usr/lib/i386-linux-gnu
COPY --from=libs-source /usr/lib/i386-linux-gnu /usr/lib/i386-linux-gnu

ENV LANG=en_US.UTF-8
USER $USER
WORKDIR $HOME
