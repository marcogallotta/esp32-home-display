import os

import pytest
from fastapi.testclient import TestClient

from app.config import load_config
from app.db import build_engine, build_session_factory
from app.main import create_app
from app.models import Base


if os.getenv("ENV", "dev") != "test":
    raise RuntimeError("Tests must be run with ENV=test")


@pytest.fixture
def app():
    config = load_config()
    engine = build_engine(config)
    session_factory = build_session_factory(engine)

    Base.metadata.drop_all(engine)
    Base.metadata.create_all(engine)

    app = create_app(config, engine, session_factory)

    try:
        yield app
    finally:
        Base.metadata.drop_all(engine)
        engine.dispose()


@pytest.fixture
def client(app):
    return TestClient(app)


@pytest.fixture
def api_key(app):
    return app.state.config.api_key


@pytest.fixture
def dashboard_password(app):
    return app.state.config.dashboard_password


@pytest.fixture
def authed_client(client, dashboard_password):
    client.post("/login", data={"password": dashboard_password})
    return client


@pytest.fixture
def db_session(app):
    session = app.state.session_factory()
    try:
        yield session
    finally:
        session.close()
