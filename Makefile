COMPOSE := docker compose -f compose.yml

.PHONY: build-base build-services build install up up-palworld down \
	restart logs status config

build-base:
	$(COMPOSE) --profile build-only build base

build-services:
	$(COMPOSE) --profile maintenance build palworld installer

build: build-base build-services

install:
	$(COMPOSE) --profile maintenance run --rm installer \
		-app 2394010 -os linux -osarch 64 \
		-dir /data/palworld -validate
	$(COMPOSE) --profile maintenance run --rm --entrypoint /bin/chmod installer \
		+x /data/palworld/Pal/Binaries/Linux/PalServer-Linux-Shipping \
		/data/palworld/Pal/Plugins/Sentry/Binaries/Linux/crashpad_handler \
		/data/palworld/PalServer.sh

up:
	$(COMPOSE) up -d palworld

up-palworld:
	$(COMPOSE) up -d palworld

down:
	$(COMPOSE) --profile maintenance down

restart:
	$(COMPOSE) restart palworld

logs:
	$(COMPOSE) logs -f palworld

status:
	$(COMPOSE) --profile maintenance ps

config:
	$(COMPOSE) config
