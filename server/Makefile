ENV ?= dev
export ENV

DB_NAME = $(shell python3 -c 'from config import load_config; print(load_config()["database"]["name"])')
DB_USER = $(shell python3 -c 'from config import load_config; print(load_config()["database"]["user"])')
DB_PORT = $(shell python3 -c 'from config import load_config; print(load_config()["database"]["port"])')
DB_PASSWORD = $(shell python3 -c 'from config import load_config; print(load_config()["database"]["password"])')

.PHONY: run-db wait-db stop-db delete-db reset-db run-server

run-db:
	@if docker ps --format '{{.Names}}' | grep -qx postgres; then \
		echo "postgres already running"; \
	elif docker ps -a --format '{{.Names}}' | grep -qx postgres; then \
		docker start postgres; \
	else \
		docker run --name postgres \
			-e POSTGRES_DB=$(DB_NAME) \
			-e POSTGRES_USER=$(DB_USER) \
			-e POSTGRES_PASSWORD=$(DB_PASSWORD) \
			-p 127.0.0.1:$(DB_PORT):5432 \
			-v pgdata:/var/lib/postgresql \
			-d postgres; \
	fi

wait-db:
	@until docker exec postgres pg_isready -U $(DB_USER) -d $(DB_NAME) >/dev/null 2>&1; do \
		sleep 0.5; \
	done

stop-db:
	docker stop postgres || true

delete-db: stop-db
	@printf "Delete postgres container? [y/N] " && \
		read ans && [ "$$ans" = "y" ] && docker rm postgres || true

reset-db: run-db wait-db
	python3 reset_db.py

run-server:
	uvicorn main:app --reload
