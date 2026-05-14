ENV ?= dev
export ENV

_DB_CONFIG  := $(shell python3 -c 'from app.config import load_config; c = load_config(); print(c.database.name, c.database.user, c.database.port, c.database.password)')
DB_NAME     := $(word 1,$(_DB_CONFIG))
DB_USER     := $(word 2,$(_DB_CONFIG))
DB_PORT     := $(word 3,$(_DB_CONFIG))
DB_PASSWORD := $(word 4,$(_DB_CONFIG))

DB_CONTAINER := postgres-$(ENV)
DB_VOLUME    := pgdata-$(ENV)

APP_IMAGE     := home-display-server
APP_CONTAINER := home-display-server

.PHONY: db-start db-up db-stop db-rm db-reset-schema \
        app-run-local app-image-build app-container-run app-container-stop \
        test test-cov _db-wait

db-start:
	@if docker ps --format '{{.Names}}' | grep -qx $(DB_CONTAINER); then \
		echo "$(DB_CONTAINER) already running"; \
	elif docker ps -a --format '{{.Names}}' | grep -qx $(DB_CONTAINER); then \
		docker start $(DB_CONTAINER); \
	else \
		docker run --name $(DB_CONTAINER) \
			-e POSTGRES_DB=$(DB_NAME) \
			-e POSTGRES_USER=$(DB_USER) \
			-e POSTGRES_PASSWORD=$(DB_PASSWORD) \
			-p 127.0.0.1:$(DB_PORT):5432 \
			-v $(DB_VOLUME):/var/lib/postgresql \
			-d postgres:16; \
	fi

db-up: db-start _db-wait

_db-wait:
	@until docker exec $(DB_CONTAINER) pg_isready -U $(DB_USER) -d $(DB_NAME) >/dev/null 2>&1; do \
		sleep 0.5; \
	done

db-stop:
	docker stop $(DB_CONTAINER) || true

db-rm:
	@printf "Remove container $(DB_CONTAINER) and volume $(DB_VOLUME)? [y/N] " && \
	read ans && [ "$$ans" = "y" ] || exit 0; \
	docker stop $(DB_CONTAINER) || true; \
	docker rm $(DB_CONTAINER) || true; \
	docker volume rm $(DB_VOLUME) || true

db-reset-schema: db-up
	@if [ "$(ENV)" != "test" ]; then \
		printf "Reset schema on $(DB_NAME) ($(ENV))? [y/N] " && \
		read ans && [ "$$ans" = "y" ] || exit 0; \
	fi
	python3 tools/reset_db.py

app-run-local:
	uvicorn app.wsgi:app --reload --host 0.0.0.0 \
		--ssl-keyfile certs/key.pem \
		--ssl-certfile certs/cert.pem \
		--log-config config/logging.yaml

app-image-build:
	docker build -t $(APP_IMAGE) .

app-container-run:
	docker run --name $(APP_CONTAINER) \
		-e ENV=$(ENV) \
		-v $(PWD)/config/$(ENV).json:/app/config/$(ENV).json:ro \
		-v $(PWD)/certs:/app/certs:ro \
		-p 443:443 \
		-d $(APP_IMAGE)

app-container-stop:
	docker stop $(APP_CONTAINER) || true
	docker rm $(APP_CONTAINER) || true

PYTEST_FLAGS ?=

test:
	ENV=test $(MAKE) db-reset-schema
	ENV=test pytest -q $(PYTEST_FLAGS)

test-cov: PYTEST_FLAGS = --cov=app --cov-report=term-missing
test-cov: test
