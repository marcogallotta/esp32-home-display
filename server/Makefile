ENV ?= dev
export ENV

DB_NAME = $(shell python3 -c 'from config import load_config; print(load_config()["database"]["name"])')
DB_USER = $(shell python3 -c 'from config import load_config; print(load_config()["database"]["user"])')
DB_PORT = $(shell python3 -c 'from config import load_config; print(load_config()["database"]["port"])')
DB_PASSWORD = $(shell python3 -c 'from config import load_config; print(load_config()["database"]["password"])')

DB_CONTAINER = postgres-$(ENV)
DB_VOLUME = pgdata-$(ENV)

.PHONY: db-start db-wait db-stop db-rm-container db-rm-volume db-rm-all db-reset-schema run-server test

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
			-d postgres; \
	fi

db-wait:
	@until docker exec $(DB_CONTAINER) pg_isready -U $(DB_USER) -d $(DB_NAME) >/dev/null 2>&1; do \
		sleep 0.5; \
	done

db-stop:
	docker stop $(DB_CONTAINER) || true

db-rm-container: db-stop
	@printf "Remove container $(DB_CONTAINER)? [y/N] " && \
	read ans && [ "$$ans" = "y" ] && docker rm $(DB_CONTAINER) || true

db-rm-volume:
	@printf "Remove volume $(DB_VOLUME)? [y/N] " && \
	read ans && [ "$$ans" = "y" ] && docker volume rm $(DB_VOLUME) || true

db-rm-all:
	@printf "Remove container $(DB_CONTAINER) and volume $(DB_VOLUME)? [y/N] " && \
	read ans && [ "$$ans" = "y" ] && { \
		docker stop $(DB_CONTAINER) || true; \
		docker rm $(DB_CONTAINER) || true; \
		docker volume rm $(DB_VOLUME) || true; \
	} || true

db-reset-schema: db-start db-wait
	python3 reset_db.py

run-server:
	uvicorn main:app --reload --host 0.0.0.0 \
		--ssl-keyfile certs/key.pem \
		--ssl-certfile certs/cert.pem \
		--log-config logging.yaml

test:
	ENV=test $(MAKE) db-reset-schema
	ENV=test pytest -q

test-cov:
	ENV=test $(MAKE) db-reset-schema
	ENV=test pytest --cov=. --cov-report=term-missing
