ENV ?= dev
export ENV

APP_IMAGE    := home-display-server
COMPOSE      := docker compose --env-file config/env -f compose.yml
TEST_COMPOSE := docker compose -f compose.test.yml

.PHONY: app-image-build up down db-reset-schema test test-cov

app-image-build:
	docker build -t $(APP_IMAGE) .

up:
	$(COMPOSE) up -d

down:
	$(COMPOSE) down

db-reset-schema: up
	@if [ "$(ENV)" != "test" ]; then \
		printf "Reset schema on $(ENV)? [y/N] " && \
		read ans && [ "$$ans" = "y" ] || exit 0; \
	fi
	$(COMPOSE) exec -e PYTHONPATH=/app app python3 tools/reset_db.py

test:
	$(TEST_COMPOSE) run --rm test
	$(TEST_COMPOSE) down -v

test-cov:
	$(TEST_COMPOSE) run --rm test pytest -q --cov=app --cov-report=term-missing
	$(TEST_COMPOSE) down -v
