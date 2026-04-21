import os

import pytest
from fastapi.testclient import TestClient

from main import create_app
from config import load_config
from db import build_engine, build_session_factory
from models import Base


@pytest.fixture
def app():
    config = load_config()
    app = create_app(config)

    engine = build_engine(config)
    session_factory = build_session_factory(engine)

    Base.metadata.drop_all(engine)
    Base.metadata.create_all(engine)

    app.state.engine = engine
    app.state.session_factory = session_factory

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
    config = load_config()
    return config["api_key"]
