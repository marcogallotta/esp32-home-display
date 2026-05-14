from alembic import command
from alembic.config import Config as AlembicConfig

from app.config import load_config
from app.db import build_engine
from app.models import Base


def main():
    engine = build_engine(load_config())
    Base.metadata.drop_all(engine)

    alembic_cfg = AlembicConfig("alembic.ini")
    command.stamp(alembic_cfg, "base")
    command.upgrade(alembic_cfg, "head")
    print("database reset complete")


if __name__ == "__main__":
    main()
