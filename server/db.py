from sqlalchemy import create_engine
from sqlalchemy.engine import URL
from sqlalchemy.orm import sessionmaker


def build_database_url(config: dict) -> URL:
    db = config["database"]
    return URL.create(
        db["driver"],
        username=db["user"],
        password=db["password"],
        host=db["host"],
        port=db["port"],
        database=db["name"],
    )


def build_engine(config: dict):
    return create_engine(build_database_url(config))


def build_session_factory(engine):
    return sessionmaker(bind=engine, autoflush=False, autocommit=False)
