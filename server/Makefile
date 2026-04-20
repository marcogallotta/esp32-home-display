include .env
export

.PHONY: run-db wait-db stop-db delete-db reset-db reset-db run-server

run-db:
	@test -f $(DB_PASSWORD_FILE) || (echo "Missing $(DB_PASSWORD_FILE)"; exit 1)
	@chmod 600 $(DB_PASSWORD_FILE)
	@if docker ps --format '{{.Names}}' | grep -qx postgres; then \
		echo "postgres already running"; \
	elif docker ps -a --format '{{.Names}}' | grep -qx postgres; then \
		docker start postgres; \
	else \
		docker run --name postgres \
			-e POSTGRES_DB=$(DB_NAME) \
			-e POSTGRES_USER=$(DB_USER) \
			-e POSTGRES_PASSWORD_FILE=/run/secrets/db_password \
			-p 127.0.0.1:$(DB_PORT):5432 \
			-v pgdata:/var/lib/postgresql \
			-v $(CURDIR)/$(DB_PASSWORD_FILE):/run/secrets/db_password:ro \
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
	@test -f $(API_KEY_FILE) || (echo "Missing $(API_KEY_FILE)"; exit 1)
	@API_KEY="$$(cat $(API_KEY_FILE))" uvicorn main:app --reload
