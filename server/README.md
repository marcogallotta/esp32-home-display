# Backend Server

FastAPI server backed by PostgreSQL. Stores sensor history uploaded by the ESP32 and serves a browser-based graph view.

## Dependencies

```bash
sudo apt install docker.io docker-compose-v2 openssl
```

## Setup

Generate TLS certs (required for HTTPS):

```bash
./tools/gen_certs.sh
```

Copy the generated cert into `data/` and reference it in `config.json` under `api.pem_file`.

Create the env file and fill in your values:

```bash
cp config/env.example config/env
```

## Running

Use `make` targets — they wrap `docker compose` with the correct `--env-file` flag:

```bash
make compose-up    # start Postgres + server
make compose-down  # stop
```

The graph UI is at `https://localhost:8000/static/overview.html`.

## Database

Reset schema (prompts for confirmation unless ENV=test):

```bash
make db-reset-schema
```

## Tests

```bash
make test
make test-cov
```
