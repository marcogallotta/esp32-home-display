# Backend Server

FastAPI server backed by PostgreSQL. Receives sensor readings from the ESP32, stores
history, and serves a browser dashboard with live readings, history graphs, and
ML-based predictions.

The server uses two auth mechanisms: an API key for the ESP32 (header `x-api-key`) and
cookie session auth for the browser dashboard. Most GET endpoints accept either.

## Dependencies

```bash
sudo apt install docker.io docker-compose-v2 openssl
```

## Setup

**1. Generate TLS certs** (required -- uvicorn will not start without them):

```bash
./tools/gen_certs.sh
```

Copy the generated cert into `data/` and set `api.pem_file` in your firmware
`config.json` to point to it.

**2. Create the env file** and fill in all values:

```bash
cp config/env.example config/env
```

The app hard-fails at startup if any required variable is missing.

**3. Create the dashboard config**:

```bash
cp static/config.js.example static/config.js
```

Edit `static/config.js` to set your poll interval and time range presets.
This file is gitignored. If it is missing, the dashboard renders an error card
immediately on load.

## Running

All `make` targets wrap `docker compose` with the correct `--env-file` flag:

```bash
make up    # build image, start Postgres and server
make down  # stop
```

After the first `make up`, apply migrations to create the schema:

```bash
docker compose --env-file config/env -f compose.yml exec app alembic upgrade head
```

The dashboard is at `https://localhost:8000/static/overview.html`.

## Database

Apply pending migrations after pulling:

```bash
docker compose --env-file config/env -f compose.yml exec app alembic upgrade head
```

Reset schema from scratch (prompts for confirmation):

```bash
make db-reset-schema
```

Note: `make test` and `make db-reset-schema` both use `Base.metadata.drop_all` +
`create_all` and bypass Alembic migrations. Migration/model drift is invisible until
you run `alembic upgrade head` against a real DB.

## Tests

Tests run in Docker against a real ephemeral PostgreSQL instance -- no mocking:

```bash
make test
make test-cov
```

## Levoit humidifier controller

The `levoit-controller` service in `compose.yml` runs a control loop that reads
the latest SwitchBot humidity from the server API and commands a Levoit humidifier
to maintain a target absolute humidity. It runs as a separate process with no direct
database access.

Config and credentials are in `server/config/env` and `server/config/app.json`.
See the root `README.md` for the full config reference.
