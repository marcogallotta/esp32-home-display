from models import Base, engine


def main():
    Base.metadata.drop_all(engine)
    Base.metadata.create_all(engine)
    print("database reset complete")


if __name__ == "__main__":
    main()
