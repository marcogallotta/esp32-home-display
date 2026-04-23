from datetime import datetime
from typing import Optional

from sqlalchemy import (
    CheckConstraint,
    DateTime,
    Float,
    ForeignKey,
    Index,
    Integer,
    SmallInteger,
    String,
    UniqueConstraint,
)
from sqlalchemy.orm import DeclarativeBase, Mapped, mapped_column


SWITCHBOT_TYPE = 1
XIAOMI_TYPE = 2


class Base(DeclarativeBase):
    pass


class Sensor(Base):
    __tablename__ = "sensors"
    __table_args__ = (
        CheckConstraint("type IN (1, 2)", name="sensors_type_valid"),
    )

    mac: Mapped[str] = mapped_column(String, primary_key=True)
    name: Mapped[Optional[str]] = mapped_column(String, nullable=True)
    type: Mapped[int] = mapped_column(SmallInteger, nullable=False)


class SwitchbotReading(Base):
    __tablename__ = "switchbot_readings"
    __table_args__ = (
        UniqueConstraint("mac", "timestamp", name="switchbot_readings_mac_timestamp_uniq"),
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
        UniqueConstraint("mac", "timestamp", name="xiaomi_readings_mac_timestamp_uniq"),
        Index("xiaomi_readings_mac_timestamp_idx", "mac", "timestamp"),
    )

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    mac: Mapped[str] = mapped_column(String, ForeignKey("sensors.mac"), nullable=False)
    timestamp: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    temperature_c: Mapped[Optional[float]] = mapped_column(Float, nullable=True)
    moisture_pct: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    light_lux: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    conductivity_us_cm: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
