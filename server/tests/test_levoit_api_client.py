from unittest.mock import MagicMock

import httpx
import pytest

from app.levoit_api_client import (
    LevoitApiClient,
    LevoitApiError,
    SwitchbotLatestReading,
    SwitchbotSensorNotFound,
)

_BASE_URL = "https://localhost:8000"
_API_KEY = "test-key"
_MAC = "AA:BB:CC:DD:EE:FF"

_SAMPLE_RESPONSE = {
    "sensors": [
        {
            "mac": "AA:BB:CC:DD:EE:FF",
            "sensor_id": "11111111-1111-1111-1111-111111111111",
            "latest_timestamp": "2026-04-21T10:00:00Z",
            "reading": {
                "temperature_c": 22.5,
                "humidity_pct": 55.0,
            },
        }
    ]
}


def _mock_http(json_body=None, *, http_error=None, request_error=None):
    response = MagicMock()
    response.json.return_value = json_body if json_body is not None else {}
    if http_error:
        response.raise_for_status.side_effect = http_error
    else:
        response.raise_for_status.return_value = None
    http = MagicMock(spec=httpx.Client)
    if request_error:
        http.get.side_effect = request_error
    else:
        http.get.return_value = response
    return http


def _client(mac=_MAC, http=None):
    return LevoitApiClient(
        base_url=_BASE_URL,
        api_key=_API_KEY,
        switchbot_mac=mac,
        http_client=http or _mock_http(json_body={"sensors": []}),
    )


# --- successful match ---

def test_successful_match_returns_reading():
    result = _client(http=_mock_http(json_body=_SAMPLE_RESPONSE)).fetch_switchbot_latest()
    assert isinstance(result, SwitchbotLatestReading)
    assert result.mac == "AA:BB:CC:DD:EE:FF"
    assert result.sensor_id == "11111111-1111-1111-1111-111111111111"
    assert result.timestamp == "2026-04-21T10:00:00Z"
    assert result.temperature_c == 22.5
    assert result.humidity_pct == 55.0


# --- MAC matching ---

def test_no_matching_mac_raises_not_found():
    body = {
        "sensors": [
            {
                "mac": "00:00:00:00:00:00",
                "sensor_id": "22222222-2222-2222-2222-222222222222",
                "latest_timestamp": "2026-04-21T10:00:00Z",
                "reading": {"temperature_c": 20.0, "humidity_pct": 50.0},
            }
        ]
    }
    with pytest.raises(SwitchbotSensorNotFound):
        _client(http=_mock_http(json_body=body)).fetch_switchbot_latest()


def test_lowercase_configured_mac_matches_uppercase_response():
    result = _client(
        mac="aa:bb:cc:dd:ee:ff",
        http=_mock_http(json_body=_SAMPLE_RESPONSE),
    ).fetch_switchbot_latest()
    assert result.mac == "AA:BB:CC:DD:EE:FF"


def test_empty_sensors_list_raises_not_found():
    with pytest.raises(SwitchbotSensorNotFound):
        _client(http=_mock_http(json_body={"sensors": []})).fetch_switchbot_latest()


# --- request shape ---

def test_request_url_is_sensors_latest_with_no_query_params():
    http = _mock_http(json_body=_SAMPLE_RESPONSE)
    _client(http=http).fetch_switchbot_latest()
    call = http.get.call_args
    assert call.args[0] == f"{_BASE_URL}/sensors/latest"
    assert not call.kwargs.get("params")


def test_api_key_header_is_sent():
    http = _mock_http(json_body=_SAMPLE_RESPONSE)
    _client(http=http).fetch_switchbot_latest()
    headers = http.get.call_args.kwargs["headers"]
    assert headers["x-api-key"] == _API_KEY


# --- nullable fields ---

def test_missing_temperature_preserved_as_none():
    body = {
        "sensors": [{
            "mac": _MAC,
            "sensor_id": "11111111-1111-1111-1111-111111111111",
            "latest_timestamp": "2026-04-21T10:00:00Z",
            "reading": {"temperature_c": None, "humidity_pct": 55.0},
        }]
    }
    result = _client(http=_mock_http(json_body=body)).fetch_switchbot_latest()
    assert result.temperature_c is None
    assert result.humidity_pct == 55.0


def test_missing_humidity_preserved_as_none():
    body = {
        "sensors": [{
            "mac": _MAC,
            "sensor_id": "11111111-1111-1111-1111-111111111111",
            "latest_timestamp": "2026-04-21T10:00:00Z",
            "reading": {"temperature_c": 22.5, "humidity_pct": None},
        }]
    }
    result = _client(http=_mock_http(json_body=body)).fetch_switchbot_latest()
    assert result.temperature_c == 22.5
    assert result.humidity_pct is None


def test_malformed_matching_entry_raises_levoit_api_error():
    body = {
        "sensors": [{
            "mac": _MAC,
            # sensor_id, latest_timestamp missing
        }]
    }
    with pytest.raises(LevoitApiError, match="malformed matching sensor"):
        _client(http=_mock_http(json_body=body)).fetch_switchbot_latest()


# --- error handling ---

def test_http_error_raises_levoit_api_error():
    mock_response = MagicMock()
    mock_response.status_code = 503
    exc = httpx.HTTPStatusError("error", request=MagicMock(), response=mock_response)
    http = _mock_http(http_error=exc)
    with pytest.raises(LevoitApiError, match="HTTP 503"):
        _client(http=http).fetch_switchbot_latest()


def test_request_error_raises_levoit_api_error():
    http = _mock_http(request_error=httpx.ConnectError("connection refused"))
    with pytest.raises(LevoitApiError, match="request error"):
        _client(http=http).fetch_switchbot_latest()


def test_malformed_response_missing_sensors_key_raises_levoit_api_error():
    http = _mock_http(json_body={"unexpected": "shape"})
    with pytest.raises(LevoitApiError, match="malformed response"):
        _client(http=http).fetch_switchbot_latest()


def test_malformed_response_non_dict_raises_levoit_api_error():
    response = MagicMock()
    response.raise_for_status.return_value = None
    response.json.side_effect = ValueError("not valid json")
    http = MagicMock(spec=httpx.Client)
    http.get.return_value = response
    with pytest.raises(LevoitApiError, match="malformed response"):
        _client(http=http).fetch_switchbot_latest()
