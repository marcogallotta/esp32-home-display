import pytest

from tests.helpers import make_switchbot_payload, post_switchbot


# --- Dashboard login ---

def test_login_accepts_correct_password(client, dashboard_password):
    response = client.post("/login", data={"password": dashboard_password}, follow_redirects=False)

    assert response.status_code == 303
    assert response.headers["location"] == "/static/overview.html"


def test_login_rejects_wrong_password(client):
    response = client.post("/login", data={"password": "wrong"}, follow_redirects=False)

    assert response.status_code == 401
    assert response.json() == {"detail": "unauthorized"}


def test_login_rejects_empty_password(client):
    response = client.post("/login", data={"password": ""}, follow_redirects=False)

    assert response.status_code == 401


# --- Dashboard read endpoints require session ---

def test_session_auth_accepts_logged_in_user(authed_client):
    response = authed_client.get("/sensors")

    assert response.status_code != 401


def test_session_auth_rejects_unauthenticated_request(client):
    response = client.get("/sensors")

    assert response.status_code == 401
    assert response.json() == {"detail": "unauthorized"}


def test_api_key_accepted_on_get_sensors(client, api_key):
    response = client.get("/sensors", headers={"x-api-key": api_key})

    assert response.status_code == 200


def test_api_key_accepted_on_get_sensors_latest(client, api_key):
    response = client.get("/sensors/latest", headers={"x-api-key": api_key})

    assert response.status_code == 200


def test_api_key_accepted_on_get_sensor_readings(client, api_key):
    post_switchbot(client, api_key, make_switchbot_payload())
    sensor_id = client.get("/sensors", headers={"x-api-key": api_key}).json()[0]["id"]

    response = client.get(f"/sensors/{sensor_id}/readings", headers={"x-api-key": api_key})

    assert response.status_code == 200


@pytest.mark.parametrize(
    "path",
    ["/sensors", "/sensors/latest", "/sensors/00000000-0000-0000-0000-000000000000/readings"],
    ids=["sensors", "latest", "readings"],
)
def test_invalid_api_key_rejected_on_sensor_read_endpoints(client, path):
    response = client.get(path, headers={"x-api-key": "wrong-key"})

    assert response.status_code == 401
    assert response.json() == {"detail": "unauthorized"}


@pytest.mark.parametrize(
    "path",
    ["/sensors", "/sensors/latest", "/sensors/00000000-0000-0000-0000-000000000000/readings"],
    ids=["sensors", "latest", "readings"],
)
def test_invalid_api_key_does_not_fall_back_to_session(authed_client, path):
    response = authed_client.get(path, headers={"x-api-key": "wrong-key"})

    assert response.status_code == 401


def test_session_alone_rejected_on_write_endpoint(authed_client):
    response = authed_client.post("/switchbot/reading", json=make_switchbot_payload())

    assert response.status_code == 401


# --- Device write endpoints require API key ---

@pytest.mark.parametrize(
    "header_value",
    [
        "wrong-api-key",
        "",
        "x" * 10_000,
        None,
    ],
    ids=[
        "wrong key",
        "empty key",
        "very long key",
        "missing key",
    ],
)
def test_api_key_auth_rejects_invalid_key_on_write_endpoint(client, header_value):
    headers = {}
    if header_value is not None:
        headers["x-api-key"] = header_value

    response = client.post("/switchbot/reading", headers=headers, json={})

    assert response.status_code == 401
    assert response.json() == {"detail": "unauthorized"}


def test_api_key_auth_accepts_correct_key_on_write_endpoint(client, api_key):
    response = client.post(
        "/switchbot/reading",
        headers={"x-api-key": api_key},
        json={
            "mac": "AA:BB:CC:DD:EE:FF",
            "name": "test",
            "timestamp": "2026-04-21T18:00:00Z",
            "temperature_c": 21.5,
            "humidity_pct": 48.0,
        },
    )

    assert response.status_code != 401


# --- Forecast / prediction read endpoints accept session or API key ---

_WEATHER_PATH = "/openmeteo/weather?start_ts=2026-06-01T00:00:00Z&end_ts=2026-06-02T00:00:00Z"
_PREDICT_PATH = "/predict/temperature"


@pytest.fixture
def stub_openmeteo(monkeypatch):
    # Both endpoints call _get_openmeteo_weather; stub it so the auth/routing
    # path is exercised without hitting the external Open-Meteo API.
    monkeypatch.setattr("app.openmeteo._get_openmeteo_weather", lambda *a, **k: [])
    monkeypatch.setattr("app.predict._get_openmeteo_weather", lambda *a, **k: [])


@pytest.mark.parametrize("path", [_WEATHER_PATH, _PREDICT_PATH], ids=["weather", "predict"])
def test_forecast_endpoints_reject_unauthenticated_request(client, path):
    response = client.get(path)

    assert response.status_code == 401
    assert response.json() == {"detail": "unauthorized"}


@pytest.mark.parametrize("path", [_WEATHER_PATH, _PREDICT_PATH], ids=["weather", "predict"])
def test_forecast_endpoints_accept_api_key(client, api_key, stub_openmeteo, path):
    response = client.get(path, headers={"x-api-key": api_key})

    assert response.status_code == 200


@pytest.mark.parametrize("path", [_WEATHER_PATH, _PREDICT_PATH], ids=["weather", "predict"])
def test_forecast_endpoints_accept_session(authed_client, stub_openmeteo, path):
    response = authed_client.get(path)

    assert response.status_code == 200


@pytest.mark.parametrize("path", [_WEATHER_PATH, _PREDICT_PATH], ids=["weather", "predict"])
def test_forecast_endpoints_reject_invalid_api_key(client, path):
    response = client.get(path, headers={"x-api-key": "wrong-key"})

    assert response.status_code == 401
    assert response.json() == {"detail": "unauthorized"}


@pytest.mark.parametrize("path", [_WEATHER_PATH, _PREDICT_PATH], ids=["weather", "predict"])
def test_forecast_endpoints_invalid_api_key_does_not_fall_back_to_session(authed_client, path):
    response = authed_client.get(path, headers={"x-api-key": "wrong-key"})

    assert response.status_code == 401


def test_openmeteo_rejects_invalid_start_ts(authed_client, monkeypatch):
    monkeypatch.setattr("app.openmeteo._get_openmeteo_weather", lambda *a, **k: pytest.fail("should not fetch"))

    response = authed_client.get(
        "/openmeteo/weather",
        params={"start_ts": "not-a-date", "end_ts": "2026-06-12T00:00:00Z"},
    )

    assert response.status_code == 400
    assert response.json() == {"detail": "start_ts must be ISO 8601"}


def test_openmeteo_rejects_naive_timestamp(authed_client, monkeypatch):
    monkeypatch.setattr("app.openmeteo._get_openmeteo_weather", lambda *a, **k: pytest.fail("should not fetch"))

    response = authed_client.get(
        "/openmeteo/weather",
        params={"start_ts": "2026-06-12T00:00:00", "end_ts": "2026-06-12T01:00:00Z"},
    )

    assert response.status_code == 400
    assert response.json() == {"detail": "start_ts must include timezone"}


def test_openmeteo_rejects_reversed_range(authed_client, monkeypatch):
    monkeypatch.setattr("app.openmeteo._get_openmeteo_weather", lambda *a, **k: pytest.fail("should not fetch"))

    response = authed_client.get(
        "/openmeteo/weather",
        params={"start_ts": "2026-06-13T00:00:00Z", "end_ts": "2026-06-12T00:00:00Z"},
    )

    assert response.status_code == 400
    assert response.json() == {"detail": "start_ts must be <= end_ts"}


def test_openmeteo_rejects_too_large_range(authed_client, monkeypatch):
    monkeypatch.setattr("app.openmeteo._get_openmeteo_weather", lambda *a, **k: pytest.fail("should not fetch"))

    response = authed_client.get(
        "/openmeteo/weather",
        params={"start_ts": "2025-01-01T00:00:00Z", "end_ts": "2026-06-12T00:00:00Z"},
    )

    assert response.status_code == 400
    assert response.json() == {"detail": "date range is too large"}


def test_openmeteo_normalizes_valid_offset_timestamps(authed_client, monkeypatch):
    captured = {}

    def fake_get(start_ts, end_ts, lat, lon):
        captured["start_ts"] = start_ts
        captured["end_ts"] = end_ts
        return []

    monkeypatch.setattr("app.openmeteo._get_openmeteo_weather", fake_get)

    response = authed_client.get(
        "/openmeteo/weather",
        params={
            "start_ts": "2026-06-12T02:00:00+02:00",
            "end_ts": "2026-06-12T04:00:00+02:00",
        },
    )

    assert response.status_code == 200
    assert captured == {
        "start_ts": "2026-06-12T00:00:00+00:00",
        "end_ts": "2026-06-12T02:00:00+00:00",
    }
