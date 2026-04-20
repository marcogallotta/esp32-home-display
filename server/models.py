from datetime import datetime
from pathlib import Path
from typing import Optional

import os

from sqlalchemy import DateTime, Float, ForeignKey, Index, Integer, SmallInteger, String, create_engine
from sqlalchemy.engine import URL
from sqlalchemy.orm import DeclarativeBase, Mapped, mapped_column, sessionmaker


DB_HOST = os.getenv("DB_HOST", "localhost")
DB_PORT = int(os.getenv("DB_PORT", "5432"))
DB_NAME = os.getenv("DB_NAME", "eps32")
DB_USER = os.getenv("DB_USER", "postgres")
DB_PASSWORD_FILE = os.getenv("DB_PASSWORD_FILE", ".secrets/db_password")

SWITCHBOT_TYPE = 1
XIAOMI_TYPE = 2


def read_db_password() -> str:
    password = Path(DB_PASSWORD_FILE).read_text(encoding="utf-8").strip()
    if not password:
        raise RuntimeError(f"{DB_PASSWORD_FILE} is empty")
    return password


DATABASE_URL = URL.create(
    "postgresql+psycopg",
    username=DB_USER,
    password=read_db_password(),
    host=DB_HOST,
    port=DB_PORT,
    database=DB_NAME,
)

engine = create_engine(DATABASE_URL)
SessionLocal = sessionmaker(bind=engine, autoflush=False, autocommit=False)


class Base(DeclarativeBase):
    pass


class Sensor(Base):
    __tablename__ = "sensors"

    mac: Mapped[str] = mapped_column(String, primary_key=True)
    name: Mapped[Optional[str]] = mapped_column(String, nullable=True)
    type: Mapped[int] = mapped_column(SmallInteger, nullable=False)


class SwitchbotReading(Base):
    __tablename__ = "switchbot_readings"
    __table_args__ = (
        Index("switchbot_readings_mac_timestamp_idx", "mac", "timestamp"),
    )

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    mac: Mapped[str] = mapped_column(String, ForeignKey("sensors.mac"), nullable=False)
    timestamp: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    temperature_c: Mapped[float] = mapped_column(Float, nullable=False)
    humidity_pct: Mapped[float] = mapped_column(Float, nullable=False)


class XiaomiReading(Base):
    __tablename__ = "xiaomi_readings"
    __table_args__ = (
        Index("xiaomi_readings_mac_timestamp_idx", "mac", "timestamp"),
    )

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    mac: Mapped[str] = mapped_column(String, ForeignKey("sensors.mac"), nullable=False)
    timestamp: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    temperature_c: Mapped[Optional[float]] = mapped_column(Float, nullable=True)
    moisture_pct: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    light_lux: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    conductivity_us_cm: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
