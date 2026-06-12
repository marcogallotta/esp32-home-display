def test_latest_empty_when_no_plant_monitor(authed_client):
    response = authed_client.get("/sensors/latest")

    assert response.status_code == 200
    body = response.json()
    assert body["sensors"] == []
    assert "retry_after_secs" in body


def test_latest_requires_session(client):
    response = client.get("/sensors/latest")

    assert response.status_code == 401
