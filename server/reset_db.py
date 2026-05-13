from alembic import command
from alembic.config import Config as AlembicConfig

from config import load_config
from db import build_engine
from models import Base


def main():
    engine = build_engine(load_config())
    Base.metadata.drop_all(engine)

    alembic_cfg = AlembicConfig("alembic.ini")
    command.upgrade(alembic_cfg, "head")
    print("database reset complete")


if __name__ == "__main__":
    main()
