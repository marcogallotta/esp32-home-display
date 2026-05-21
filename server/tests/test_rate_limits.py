from contextlib import contextmanager

import pytest
from fastapi.testclient import TestClient

from app.config import Esp32AppRateLimits, RateLimit, RateLimitsConfig
from app.config import load_config
from app.db import build_engine, build_session_factory
from app.main import create_app
from app.models import Base
from tests.helpers import (
    auth_headers,
    make_xiaomi_payload,
    post_switchbot_bulk,
    resolve_switchbot_sensor_id,
)


def _bulk_reading():
    return {"timestamp": "2026-04-21T18:00:00Z", "temperature_c": 21.5, "humidity_pct": 48.0}


@contextmanager
def _client(*, login=99, frontend=99, esp32_read=99, esp32_live_write=99, esp32_bulk_write=99):
    config = load_config()
    config.rate_limits = RateLimitsConfig(
        esp32_app=Esp32AppRateLimits(
            read=RateLimit(limit=esp32_read, period=60),
            live_write=RateLimit(limit=esp32_live_write, period=60),
            bulk_write=RateLimit(limit=esp32_bulk_write, period=60),
            burst=True,
        ),
        frontend=RateLimit(limit=frontend, period=60),
        login=RateLimit(limit=login, period=60),
    )
    engine = build_engine(config)
    session_factory = build_session_factory(engine)
    Base.metadata.drop_all(engine)
    Base.metadata.create_all(engine)
    app = create_app(config, engine, session_factory)
    try:
        yield TestClient(app), config.api_key
    finally:
        Base.metadata.drop_all(engine)
        engine.dispose()


def test_login_gets_429_after_limit():
    with _client(login=2) as (client, _):
        for _ in range(2):
            r = client.post("/login", data={"password": "wrong"}, follow_redirects=False)
            assert r.status_code != 429
        r = client.post("/login", data={"password": "wrong"}, follow_redirects=False)
        assert r.status_code == 429


def test_frontend_limit_applies():
    with _client(frontend=2) as (client, _):
        config = load_config()
        client.post("/login", data={"password": config.dashboard_password}, follow_redirects=False)
        for _ in range(2):
            r = client.get("/sensors")
            assert r.status_code != 429
        r = client.get("/sensors")
        assert r.status_code == 429


def test_esp32_read_and_live_write_buckets_are_independent():
    with _client(esp32_read=3, esp32_live_write=1) as (client, api_key):
        headers = auth_headers(api_key)
        reading = {
            "mac": "AA:BB:CC:DD:EE:FF",
            "name": "loc",
            "timestamp": "2026-04-21T18:00:00Z",
            "temperature_c": 21.5,
            "humidity_pct": 48.0,
        }

        # Drain the live_write bucket
        r = client.post("/switchbot/reading", headers=headers, json=reading)
        assert r.status_code == 200
        r = client.post("/switchbot/reading", headers=headers, json=reading)
        assert r.status_code == 429

        # Read bucket is unaffected
        r = client.post("/switchbot/sensors", headers=headers, json={"sensors": [{"mac": "AA:BB:CC:DD:EE:FF"}]})
        assert r.status_code == 200


def test_switchbot_and_xiaomi_live_writes_share_live_write_bucket():
    with _client(esp32_read=5, esp32_live_write=2) as (client, api_key):
        headers = auth_headers(api_key)
        reading = {
            "mac": "AA:BB:CC:DD:EE:FF",
            "name": "loc",
            "timestamp": "2026-04-21T18:00:00Z",
            "temperature_c": 21.5,
            "humidity_pct": 48.0,
        }
        xiaomi_payload = make_xiaomi_payload(mac="BB:BB:CC:DD:EE:FF")

        r1 = client.post("/switchbot/reading", headers=headers, json=reading)
        assert r1.status_code == 200
        r2 = client.post("/xiaomi/reading", headers=headers, json=xiaomi_payload)
        assert r2.status_code == 200
        r3 = client.post("/xiaomi/reading", headers=headers, json=xiaomi_payload)
        assert r3.status_code == 429


def test_bulk_write_bucket_enforces_its_own_limit():
    with _client(esp32_read=5, esp32_bulk_write=1) as (client, api_key):
        sensor_id = resolve_switchbot_sensor_id(client, api_key)

        r = post_switchbot_bulk(client, api_key, sensor_id, [_bulk_reading()])
        assert r.status_code == 200
        r = post_switchbot_bulk(client, api_key, sensor_id, [_bulk_reading()])
        assert r.status_code == 429


def test_bulk_write_bucket_is_independent_of_live_write_bucket():
    with _client(esp32_read=5, esp32_live_write=1, esp32_bulk_write=2) as (client, api_key):
        sensor_id = resolve_switchbot_sensor_id(client, api_key)
        headers = auth_headers(api_key)
        reading = {
            "mac": "AA:BB:CC:DD:EE:FF",
            "name": "loc",
            "timestamp": "2026-04-21T18:00:00Z",
            "temperature_c": 21.5,
            "humidity_pct": 48.0,
        }

        # Drain the live_write bucket
        r = client.post("/switchbot/reading", headers=headers, json=reading)
        assert r.status_code == 200
        r = client.post("/switchbot/reading", headers=headers, json=reading)
        assert r.status_code == 429

        # Bulk bucket is unaffected
        r = post_switchbot_bulk(client, api_key, sensor_id, [_bulk_reading()])
        assert r.status_code == 200


def test_retry_after_header_present():
    with _client(login=1) as (client, _):
        client.post("/login", data={"password": "wrong"}, follow_redirects=False)  # drain
        r = client.post("/login", data={"password": "wrong"}, follow_redirects=False)
        assert r.status_code == 429
        assert "retry-after" in r.headers
        assert int(r.headers["retry-after"]) > 0


def test_sensor_get_with_api_key_uses_esp32_read_bucket():
    with _client(esp32_read=2, frontend=99) as (client, api_key):
        headers = auth_headers(api_key)
        for _ in range(2):
            r = client.get("/sensors", headers=headers)
            assert r.status_code != 429
        r = client.get("/sensors", headers=headers)
        assert r.status_code == 429


def test_sensor_get_with_session_uses_frontend_bucket():
    with _client(esp32_read=99, frontend=2) as (client, _):
        config = load_config()
        client.post("/login", data={"password": config.dashboard_password}, follow_redirects=False)
        for _ in range(2):
            r = client.get("/sensors")
            assert r.status_code != 429
        r = client.get("/sensors")
        assert r.status_code == 429


def test_sensor_get_api_key_does_not_consume_frontend_bucket():
    with _client(esp32_read=99, frontend=1) as (client, api_key):
        headers = auth_headers(api_key)
        for _ in range(5):
            r = client.get("/sensors", headers=headers)
            assert r.status_code != 429
