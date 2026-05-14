from config import load_config
from db import build_engine, build_session_factory
from main import create_app

config = load_config()
engine = build_engine(config)
session_factory = build_session_factory(engine)
app = create_app(config, engine, session_factory)
