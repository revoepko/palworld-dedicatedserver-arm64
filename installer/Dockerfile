# syntax=docker/dockerfile:1

FROM debian:trixie-slim

ARG DEPOT_DOWNLOADER_VERSION=3.4.0

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        libicu76 \
        unzip \
        wget \
    && wget --progress=dot:giga \
        "https://github.com/SteamRE/DepotDownloader/releases/download/DepotDownloader_${DEPOT_DOWNLOADER_VERSION}/DepotDownloader-linux-arm64.zip" \
        -O /tmp/DepotDownloader.zip \
    && unzip /tmp/DepotDownloader.zip -d /opt/depotdownloader \
    && chmod +x /opt/depotdownloader/DepotDownloader \
    && ln -s /opt/depotdownloader/DepotDownloader /usr/local/bin/DepotDownloader \
    && rm /tmp/DepotDownloader.zip \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /data/palworld
ENTRYPOINT ["/usr/local/bin/DepotDownloader"]
