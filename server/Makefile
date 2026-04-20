include .env
export

.PHONY: run-db stop-db delete-db create-schema create-db wait-db clean-db reset-db

run-db:
	@if docker ps --format '{{.Names}}' | grep -qx postgres; then \
		echo "postgres already running"; \
	elif docker ps -a --format '{{.Names}}' | grep -qx postgres; then \
		docker start postgres; \
	else \
		docker run --name postgres \
		  	-e POSTGRES_DB=$(DB_NAME) \
			-e POSTGRES_HOST_AUTH_METHOD=trust \
			-p 127.0.0.1:$(DB_PORT):5432 \
		  	-v pgdata:/var/lib/postgresql \
		  	-d postgres; \
	fi

wait-db:
	@until docker exec postgres pg_isready -U $(DB_USER) >/dev/null 2>&1; do \
		sleep 0.5; \
	done

stop-db:
	docker stop postgres || true

delete-db: stop-db
	@printf "Delete postgres container? [y/N] " && \
		read ans && [ "$$ans" = "y" ] && \
		docker rm postgres || true

create-schema: run-db wait-db
	docker exec -i postgres sh -c 'psql \
		-U "$(DB_USER)" \
		-d "$(DB_NAME)"' < schema.sql

clean-db: run-db wait-db
	@printf "Drop db tables? [y/N] " && \
		read ans && [ "$$ans" = "y" ] && \
		docker exec -i postgres sh -c 'psql \
			-U "$(DB_USER)" \
			-d "$(DB_NAME)"' < drop.sql

reset-db: clean-db create-schema
