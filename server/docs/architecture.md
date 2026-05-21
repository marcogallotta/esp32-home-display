# Server Architecture

**Editing rules:** ASCII only -- no Unicode symbols. This file is a living reference; delete completed items, rewrite state sections, never mark things done inline.

---

## First-time setup

1. Copy `config/env.example` to `config/env` and fill in all values. `compose-up` reads this file via `--env-file`; the app hard-fails at startup if any required var is missing.
2. Copy `static/config.js.example` to `static/config.js` and set `latestPollMs`, `staleAfterMs`, and `rangeConfig`. If `config.js` is missing the dashboard renders an error card immediately.
3. Run `tools/gen_certs.sh` to create local TLS certs under `certs/` (gitignored; required by uvicorn).
4. Run `make compose-up` to build the image and start the app and DB.
5. Run migrations: `docker compose --env-file config/env -f compose.yml exec app alembic upgrade head`

---

## Module map

```
server/
  app/
    config.py          -- load_config(), validate_config(), Config dataclass
    db.py              -- build_engine(), build_session_factory()
    main.py            -- create_app(); all routes; SENSOR_SPECS / SENSOR_TYPE_NAMES
    wsgi.py            -- ASGI entry point for uvicorn
    models.py          -- SQLAlchemy ORM: Sensor, SwitchbotReading, XiaomiReading
    sensor_spec.py     -- SensorSpec, DataField (generic; no device logic here)
    service.py         -- all DB logic; ingest_reading, bulk_result_counts, fetch_*
    common.py          -- MAC validation, timestamp normalization, range helpers, constants
    errors.py          -- BadRequestError, UnauthorizedError, ServerMisconfiguredError
    api_limits.py      -- TokenBucketLimiter, MemoryMapStore, make_rate_limiter
    switchbot.py       -- ReadingIn/Out, SensorsIn/Out, BulkIn/Out, SENSOR spec
    xiaomi.py          -- ReadingIn/Out, SENSOR spec
  config/
    app.json           -- non-secret config (rate limits, SwitchBot tuning, session_secure)
    env                -- secrets and DB connection env vars (gitignored)
    env.example        -- template for env
    logging.yaml       -- uvicorn log config
  alembic/
    versions/          -- migration files; one per schema change
  conftest.py          -- app/client/api_key/authed_client/db_session fixtures (root-level, not under tests/)
  tests/
    helpers.py         -- shared payload builders and HTTP helpers
    test_*.py          -- one file per feature area
  compose.yml          -- dev: app + db services
  compose.test.yml     -- test: isolated app + db, ephemeral
  Makefile             -- compose-up, compose-down, test, test-cov, db-reset-schema
```

---

## Config loading

Two-layer system. Non-secrets live in `config/app.json`; secrets come from env vars.

`load_config()` (`app/config.py`):
1. Reads `config/app.json` -- `session_secure`, DB driver, rate limits, SwitchBot knobs.
2. Pulls `API_KEY`, `SESSION_SECRET`, `DASHBOARD_PASSWORD`, `DATABASE_*` from env via `_require_env()`. Hard-fails if any is missing.
3. Calls `validate_config(config, env)` -- type checks, non-empty checks, int range checks; in `prod` env also rejects known-weak defaults and enforces `session_secure=true`.

`ENV` env var selects the environment (`dev` / `test` / `prod`). Tests **must** run with `ENV=test`; `conftest.py` raises at import if not.

The loaded `Config` is attached to `app.state.config` at startup and accessed via `request.app.state.config` at request time. No global config singleton.

---

## App wiring

`create_app(config, engine, session_factory)` in `app/main.py`:
- Constructs all rate limiters from config.
- Attaches `config`, `engine`, `session_factory` to `app.state`.
- Registers `SessionMiddleware` (session secret, HTTPS-only flag from config).
- Mounts `/static`.
- Registers exception handlers for `BadRequestError` (400), `UnauthorizedError` (401), `ServerMisconfiguredError` (500).
- Builds three sub-routers: `device` (requires `x-api-key`), `dashboard` (requires session + frontend rate limit), and `sensor_router` (requires session OR API key via `sensor_read_limit`, which dispatches to the appropriate rate-limit bucket).

DB sessions are request-scoped: `get_db()` is a FastAPI dependency that creates and closes a session per request.

---

## SensorSpec pattern

Adding a device type means wiring one `SensorSpec` instance and it propagates everywhere generically.

```python
SensorSpec(
    db_sensor_type=int,           # must match models.py constant and CheckConstraint
    reading_model=SomeReading,    # SQLAlchemy model class
    reading_out=SomeReadingOut,   # Pydantic response model
    unique_constraint_name=str,   # (mac, timestamp) unique constraint name on readings table
    data_fields=(DataField(...),) # drives range validation, soft-range warnings, upsert merge logic, fetch queries
)
```

`DataField` fields:
- `name`: column name string, used as dict key throughout. **Must match the ORM column name, the Pydantic field name, and the JSON key exactly.** If it drifts, generic merge, fetch, latest, and output shape all break silently.
- `column`: SQLAlchemy `InstrumentedAttribute`, used in queries and upsert set-clauses.
- `getter`: `lambda r: r.field` -- extracts value from a `ReadingIn` instance.
- `hard_range`: out-of-range raises 422 at ingest time (enforced in `ReadingIn.model_validator`).
- `soft_range`: out-of-range logs a warning but does not reject.

`SENSOR_SPECS` in `main.py` maps `db_sensor_type -> SensorSpec`. The dashboard's generic endpoints (`/sensors/latest`, `/sensors/{id}/readings`) iterate or look up this dict. Adding a device requires adding it here and to `SENSOR_TYPE_NAMES`.

Cross-file type name invariant: the integer constant (`models.FOO_TYPE`), the `SENSOR_SPECS` key, the `SENSOR_TYPE_NAMES` value, and the frontend `sensor.type === "foo"` comparisons must all be consistent. Drift between any of these causes silent misrouting or missing data in the dashboard.

`common.SensorType` (a str enum in `common.py`) exists but is currently unused. Do not add a device only to `common.SensorType`; the active routing source is `SENSOR_SPECS` / `SENSOR_TYPE_NAMES`.

---

## Ingest path (live readings)

`POST /foo/reading -> ingest_reading(db, reading, sensor)` in `service.py`:

1. `prepare_reading` -- normalizes MAC to uppercase colon format, normalizes timestamp to UTC, logs soft-range warnings.
2. `ensure_sensor` -- upserts the `sensors` row. Requires `name` on first seen; reconciles name changes on subsequent calls (logs a warning, updates the name).
3. `get_existing_values` -- reads the current row for `(mac, timestamp)` before the upsert, capturing the pre-state.
4. `execute_reading_upsert` -- PostgreSQL `INSERT ... ON CONFLICT DO UPDATE` with `COALESCE` merge: only fills in NULL columns from the incoming row, never overwrites existing non-NULL values. `WHERE` clause ensures the update only fires when at least one merge is actually needed (avoids spurious xmax writes). Returns `xmax = 0` (inserted) or `xmax != 0` (updated).
5. `classify_existing_reading` -- compares pre-state captured in step 3 against the incoming reading to determine `created / duplicate / merged / conflict / merged_with_conflict`.

The `result` field in the response maps to firmware behavior:
- `duplicate` -- server already has identical data; firmware updates `lastSent` and moves on.
- `conflict` -- server has a different value at that timestamp; firmware resets `lastSent` to force a re-evaluation next tick.

**Live ingest conflict response is HTTP 200, not HTTP 409.** The `result` field carries the semantic; the status code is always 200 for a successfully processed request. This differs from REST convention and is easy to misread.

`execute_reading_upsert()` return values: the function returns the row with `inserted=True` for a new insert, `inserted=False` for an actual merge/update, or `None` when the `ON CONFLICT DO UPDATE ... WHERE` clause finds nothing to update (the row already exists and has no NULL columns to fill). `classify_existing_reading` handles the `None` case by classifying the result as `duplicate` or `conflict` based on the pre-upsert values captured by `get_existing_values`.

---

## Bulk ingest path (SwitchBot history sync)

Two-step protocol:

**Step 1: `POST /switchbot/sensors`**
- Upserts sensors, returns `sync_intervals` per sensor: time ranges where the server has no readings (gaps larger than `gap_threshold_minutes`).
- Gap detection reads up to `SYNC_TIMESTAMPS_MAX = 20_000` timestamps within `SYNC_RETENTION_DAYS = 68` days.
- Intervals are capped per-sensor (`max_intervals_per_sensor`) and globally (`max_intervals_total`). If capped, `sync_intervals_capped=true` and a warning is returned -- client should upload the returned intervals and call again.

**Step 2: `POST /switchbot/bulk`**
- Accepts `{sensor_id, readings: [...]}`. `sensor_id` must be a UUID from step 1.
- Validates `len(readings) <= max_readings` (config-tunable, default 200).
- Iterates readings with savepoints: each reading is ingested as a nested transaction; failure on one reading does not roll back others.
- Returns aggregate counts: `created`, `duplicate`, `conflict`, `invalid`, `errors` (first 20 error details), `failed`.

Bulk result counter semantics (non-obvious):
- `merged` increments the `created` response counter, not a separate counter -- it is a partial update, not an insert, but the client-visible count treats it as created.
- `conflict` and `merged_with_conflict` both increment `conflict` and also increment `succeeded` -- the row was processed without error, the conflicting values were just ignored.
- Conflict details appear in `errors`, but the index is NOT added to `failed`. `failed` is reserved for rows that could not be processed at all (e.g. unexpected exceptions).

---

## Fetch / query path

`GET /sensors/latest` accepts one optional filter:
- `sensor_id=<uuid>` -- return only the sensor with that UUID. Unknown UUID returns 200 with `{"sensors": []}`.

Filtering is applied inside `fetch_latest_readings` at the DB layer.

`GET /sensors/{sensor_id}/readings` dispatches to one of two modes:

- **Raw mode** (`before`/`after` params): `fetch_raw_readings` -- simple range query, `DESC`, limited.
- **Window mode** (`start_ts`/`end_ts` required, `max_points` optional): `fetch_window_readings` -- if `max_points` is set, buckets the window into `ceil(window_seconds / max_points)` second slots and picks the latest reading per bucket via `ROW_NUMBER() OVER (PARTITION BY bucket ORDER BY timestamp DESC)`.

`start_ts`/`end_ts` and `before`/`after` cannot be combined. `max_points` requires `start_ts`/`end_ts`.

---

## Database schema

```
sensors
  mac         TEXT  PK
  id          UUID  unique, not null   -- stable foreign key target for readings
  name        TEXT  not null
  type        SMALLINT not null        -- CheckConstraint: type IN (1, 2)

switchbot_readings
  mac         FK -> sensors.mac
  sensor_id   FK -> sensors.id
  timestamp   TIMESTAMPTZ not null
  temperature_c  FLOAT  not null
  humidity_pct   FLOAT  not null
  UNIQUE (mac, timestamp)

xiaomi_readings
  mac         FK -> sensors.mac
  sensor_id   FK -> sensors.id
  timestamp   TIMESTAMPTZ not null
  temperature_c    FLOAT  nullable
  moisture_pct     INT    nullable
  light_lux        INT    nullable
  conductivity_us_cm INT  nullable
  UNIQUE (mac, timestamp)
```

Both readings tables have two indexes: `(mac, timestamp)` for ingest lookups and `(sensor_id, timestamp)` for dashboard fetch queries.

Sensors carry both `mac` (PK, human-readable) and `id` (UUID, stable identifier). Readings FK to both; dashboard queries use `sensor_id`; ingest uses `mac`.

---

## Rate limiting

`TokenBucketLimiter` backed by an in-memory `MemoryMapStore`. Three buckets for the device (ESP32) client, keyed globally (not per-IP):
- `esp32_app:read` -- `POST /switchbot/sensors` and API-key-authenticated sensor GET reads (`GET /sensors`, `/sensors/latest`, `/sensors/{id}/readings`).
- `esp32_app:live_write` -- SwitchBot/Xiaomi live readings.
- `esp32_app:bulk_write` -- SwitchBot bulk.

Two buckets for the browser frontend, keyed per-IP:
- `frontend` -- session-authenticated requests: sensor GET reads, openmeteo, predict.
- `login` -- POST /login.

All limits and periods are config-tunable via `app.json`. `burst=true` allows an initial burst up to the full limit. The store is in-process; limits reset on restart and are not shared across replicas.

---

## Authentication

Two independent auth systems:

- **Device (ESP32):** `x-api-key` header, validated in `require_api_key()`. Constant-time string compare via `verify_api_key()`. Applied to the `device` router.
- **Dashboard (browser):** Cookie session via `SessionMiddleware`. `POST /login` validates `dashboard_password` and sets `session["authenticated"] = True`. `require_session()` checks this flag. Applied to openmeteo and predict routers, and to generic sensor GET endpoints when no API key is supplied.

`session_secure=true` in prod enforces HTTPS-only cookies.

**Generic sensor GET endpoints** (`GET /sensors`, `/sensors/latest`, `/sensors/{id}/readings`) accept either mechanism via `require_session_or_api_key()`:

1. If `x-api-key` is present, validate it. An invalid key raises 401 immediately -- it does NOT fall back to the session cookie. A request with an invalid key plus a valid session is still rejected.
2. If `x-api-key` is absent, require a valid dashboard session.

Returns `"api_key"` or `"session"` so `sensor_read_limit` can select the correct rate-limit bucket. The API key therefore grants read access to sensor data in addition to device write access.

---

## Testing

All tests run in Docker (`make test`) with `ENV=test` against a real ephemeral PostgreSQL instance (`compose.test.yml`). No mocking of the database.

**Alembic migrations are not tested by the test suite.** The `app` fixture uses `Base.metadata.drop_all` + `create_all`, which builds schema directly from SQLAlchemy models. Model/schema tests can pass while migrations are broken. Any schema change (new device type, new column) requires explicit Alembic review or a dedicated migration test.

`conftest.py` fixtures (root-level, picked up by all tests):
- `app` -- full app with fresh schema (drop + create_all before each test, drop_all after).
- `client` -- `TestClient(app)`.
- `api_key` / `dashboard_password` -- pulled from `app.state.config`.
- `authed_client` -- client that has already posted to `/login`.
- `db_session` -- raw SQLAlchemy session for direct DB assertions.

`tests/helpers.py` -- shared payload builders (`make_switchbot_payload`, `make_xiaomi_payload`) and HTTP wrappers (`post_switchbot`, `post_switchbot_bulk`, etc.).

Test file naming: one file per feature area (`test_switchbot_ingest.py`, `test_xiaomi_ingest.py`, `test_readings_get.py`, etc.).

---

## Alembic migrations

Migrations run inside Docker against the dev DB. The `alembic/env.py` calls `load_config()`, so it needs the `config/env` vars on the path.

**Apply pending migrations (normal workflow after pulling):**
```
docker compose --env-file config/env -f compose.yml exec app alembic upgrade head
```

**Create a new migration after changing `models.py`:**
`compose.yml` mounts `./alembic` read-only, so `alembic revision --autogenerate` cannot write inside the running app container. Either temporarily change the dev compose mount to `./alembic:/app/alembic` (drop `:ro`) while generating, or run alembic directly on the host against the DB with the env vars set. After generating, review and edit the file in `alembic/versions/` -- autogenerate misses some things (e.g. `CheckConstraint` changes, index additions). Always read the diff before committing.

**Check current migration state:**
```
docker compose --env-file config/env -f compose.yml exec app alembic current
```

**Note:** `make test` and `make db-reset-schema` both bypass migrations (they use `Base.metadata.drop_all` + `create_all`). Model/migration drift is invisible until you run `alembic upgrade head` against a real DB that already has data.

---

## Dashboard frontend (static/)

Served by FastAPI's `StaticFiles` mount at `/static`. No build step -- plain JS files loaded directly in the browser. React, Chart.js, and `chartjs-plugin-zoom` are loaded from CDN in `overview.html`. If the zoom plugin CDN is unavailable, chart zoom silently breaks (no JS error, the plugin just does nothing).

### File roles

```
static/
  overview.html      -- dashboard shell; loads CDN scripts then local JS in dependency order
  login.html         -- login form; posts to /login
  config.js          -- runtime config (gitignored); set latestPollMs, staleAfterMs, rangeConfig
  config.js.example  -- template for config.js
  api.js             -- window.api: fetchSensors, fetchSensorReadings, fetchLatestReadings
  sensor-model.js    -- window.sensorModel: data transforms and summary builders
  metrics.js         -- window.metrics: math (abs humidity, VPD) and formatting helpers
  chart-factory.js   -- window.chartFactory: Chart.js wrapper; lineChart, destroy, zoom state
  components.js      -- window.SensorCard, window.ChartCard React components
  app.jsx            -- window.App React component; main dashboard logic
  theme.css          -- all styles
```

Load order in `overview.html` matters: `config.js` -> `api.js` -> `metrics.js` -> `sensor-model.js` -> `chart-factory.js` -> `components.js` -> `app.jsx` (Babel transform). Each file writes to `window.*`; `app.jsx` consumes all of them. Note `metrics.js` loads before `sensor-model.js` because `sensor-model.js` calls `window.metrics`.

### Config.js (runtime config, not in git)

`static/config.js` is gitignored. Copy from `config.js.example` and edit. Must define `window.CONFIG`:

```js
window.CONFIG = {
  staleAfterMs: 2 * 60 * 60 * 1000,   // card shows stale indicator after this
  latestPollMs: 60 * 1000,             // how often /sensors/latest is polled
  rangeConfig: {
    "24h": { hours: 24, maxPoints: 96 },
    "3d":  { hours: 72, maxPoints: 144 },
    ...
  },
};
```

If `config.js` is missing, `app.jsx` renders an error card immediately.

### Data flow in the dashboard

1. On mount: `GET /sensors` -> populates sensor list.
2. On range change or sensor load: `GET /sensors/{id}/readings?start_ts=...&end_ts=...&max_points=N` for each sensor -> `historyBySensorId`.
3. Every `latestPollMs`: `GET /sensors/latest` -> `sensorModel.mergeLatestIntoRows()` patches `historyBySensorId` in place so charts stay live without a full reload.
4. On chart zoom: re-fetches the zoomed window with adjusted `maxPoints`; result is cached in `zoomCacheRef` so panning within the same zoom window avoids redundant fetches.

`sensorModel.normalizeReadings()` reverses server responses (server returns DESC, charts want ASC).

### Charts

`chart-factory.js` wraps Chart.js. Each chart is a line chart with time-series X axis. `lineChart(canvas, chartRef, datasets, yLabel, rangeWindow)` creates or updates the chart in place (destroys and recreates if the ref is stale). Zoom is via the Chart.js zoom plugin; `setZoomChangeCallback` wires the zoom event to the fetch-on-zoom logic in `app.jsx`.

Derived charts (abs humidity, VPD) are hidden by default and computed client-side from temperature + humidity. They are rendered only when `showDerivedCharts` is true, and destroyed when hidden.

### Adding a new device to the dashboard

1. `sensor-model.js` -- add `buildFooSummary(sensor, rows)` and extend `splitByType` to include `foo`.
2. `components.js` -- extend `SensorCard` to handle `kind="foo"`, add display fields.
3. `app.jsx`:
   - Add state for Foo sensors, a `fooSummary` memo.
   - Add a `fooCanvas`/`fooChart` ref pair and include it in the `resetZoom([...])` chart ref list.
   - Add the sensor to the `sensorGroups` destructuring.
   - Decide whether the new type is user-selectable (like SwitchBot) or always shown (like Xiaomi).
   - If the device has multiple measurement fields, add a metric selector (model on Xiaomi).
   - Add a `useEffect` to render the chart and a section in the JSX.
4. `config.js.example` -- no changes needed unless you add new config knobs.

---

## Adding a new device type

**models.py:**
- Add `FOO_TYPE = N` constant (next integer).
- Add `FooReading` SQLAlchemy model with `(mac, timestamp)` unique constraint and both indexes.
- Add `FOO_TYPE` to the `CheckConstraint` on `sensors`.

**foo.py (new file, copy xiaomi.py as template):**
- `ReadingIn` Pydantic model with `mac`, `name`, `timestamp`, and device fields.
- `ReadingOut` Pydantic model.
- `SENSOR = SensorSpec(...)` with all five fields filled in.
- Hard/soft ranges on each `DataField`.

**main.py:**
- `from . import foo`
- Add `FOO_TYPE: foo.SENSOR` to `SENSOR_SPECS`.
- Add `FOO_TYPE: "foo"` to `SENSOR_TYPE_NAMES`.
- Add `POST /foo/reading` route on the `device` router.

**alembic/versions/:**
- New migration: add `FOO_TYPE` to the `CheckConstraint`, create `foo_readings` table.

**tests:**
- `tests/test_foo_ingest.py` -- live ingest: created/duplicate/conflict/merge cases.
- Extend `tests/helpers.py` with `make_foo_payload` and `post_foo`.
- `tests/test_config_validation.py` -- if the device adds config knobs.

If the new type should appear in the dashboard, update the frontend static files as described in the "Adding a new device to the dashboard" section above. API-only devices require no frontend changes.

---

## Invariants

- `sensors.mac` is always normalized: uppercase, colon-separated, exactly 17 chars. `validate_mac_address()` in `common.py` enforces this at every entry point.
- `SENSOR_TYPE_NAMES` values are the `type` strings returned by `/sensors` and compared in the frontend (`sensor.type === "foo"`). Changing a value is a frontend compatibility break.
- Timestamps are always stored as UTC. `normalize_timestamp_to_utc()` rejects naive datetimes and converts any tz-aware datetime to UTC.
- The upsert merge (`COALESCE`) only fills NULL columns; it never overwrites an existing non-NULL value. This lets Xiaomi sensors post partial readings (one field per BLE frame) and have them merge into a complete row.
- `ensure_sensor` requires `name` on first ingest for live readings. Bulk ingest (`bulk_result_counts`) bypasses this by setting `name=None` and using the MAC already resolved from `sensor_id`.
- `classify_existing_reading` reads the pre-upsert state (`get_existing_values`) before executing the upsert, so the conflict/merge classification is based on what was in the DB *before* the merge, not after.
- `common.SensorType` is a str enum that exists in `common.py` but is unused. When adding a device type, update or remove it to keep it from drifting. The active routing source is `SENSOR_SPECS` / `SENSOR_TYPE_NAMES` in `main.py`, not `common.SensorType`.
