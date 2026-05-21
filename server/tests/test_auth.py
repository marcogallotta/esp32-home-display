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
