import pytest


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


def test_session_auth_rejects_api_key_on_read_endpoint(client, api_key):
    response = client.get("/sensors", headers={"x-api-key": api_key})

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
