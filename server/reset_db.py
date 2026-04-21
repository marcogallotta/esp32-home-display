from config import load_config
from db import build_engine
from models import Base


def main():
    engine = build_engine(load_config())
    Base.metadata.drop_all(engine)
    Base.metadata.create_all(engine)
    print("database reset complete")


if __name__ == "__main__":
    main()
