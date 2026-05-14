import os

import pytest
from fastapi.testclient import TestClient

from config import load_config
from db import build_engine, build_session_factory
from main import create_app
from models import Base

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
def api_key():
    return load_config().api_key


@pytest.fixture
def dashboard_password():
    return load_config().dashboard_password


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
