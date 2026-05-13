from tests.helpers import (
    make_switchbot_payload,
    make_xiaomi_payload,
    post_switchbot,
    post_xiaomi,
)

_EXPECTED_KEYS = {"result", "warnings"}


def test_switchbot_single_ingest_response_has_exact_keys(client, api_key):
    response = post_switchbot(client, api_key, make_switchbot_payload())

    assert response.status_code == 200
    assert set(response.json().keys()) == _EXPECTED_KEYS


def test_xiaomi_single_ingest_response_has_exact_keys(client, api_key):
    response = post_xiaomi(client, api_key, make_xiaomi_payload())

    assert response.status_code == 200
    assert set(response.json().keys()) == _EXPECTED_KEYS


def test_switchbot_single_ingest_warnings_is_always_list(client, api_key):
    response = post_switchbot(client, api_key, make_switchbot_payload())

    assert response.status_code == 200
    assert isinstance(response.json()["warnings"], list)


def test_xiaomi_single_ingest_warnings_is_always_list(client, api_key):
    response = post_xiaomi(client, api_key, make_xiaomi_payload())

    assert response.status_code == 200
    assert isinstance(response.json()["warnings"], list)


def test_switchbot_duplicate_response_shape(client, api_key):
    payload = make_switchbot_payload()
    post_switchbot(client, api_key, payload)
    response = post_switchbot(client, api_key, payload)

    assert response.status_code == 200
    assert response.json() == {"result": "duplicate", "warnings": []}


def test_xiaomi_duplicate_response_shape(client, api_key):
    payload = make_xiaomi_payload()
    post_xiaomi(client, api_key, payload)
    response = post_xiaomi(client, api_key, payload)

    assert response.status_code == 200
    assert response.json() == {"result": "duplicate", "warnings": []}
