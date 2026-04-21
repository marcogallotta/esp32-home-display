import pytest


def test_api_key_auth_accepts_correct_key(client, api_key):
    response = client.get(
        "/switchbot/readings",
        params={"mac": "AA:BB:CC:DD:EE:FF"},
        headers={"x-api-key": api_key},
    )

    assert response.status_code != 401


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
def test_api_key_auth_rejects_invalid_key(client, header_value):
    headers = {}
    if header_value is not None:
        headers["x-api-key"] = header_value

    response = client.get(
        "/switchbot/readings",
        params={"mac": "AA:BB:CC:DD:EE:FF"},
        headers=headers,
    )

    assert response.status_code == 401
    assert response.json() == {"detail": "unauthorized"}
