from datetime import datetime
from typing import Optional
from uuid import UUID, uuid4

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
from sqlalchemy.dialects.postgresql import UUID as PostgresUUID
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
    id: Mapped[UUID] = mapped_column(
        PostgresUUID(as_uuid=True),
        unique=True,
        nullable=False,
        default=uuid4,
    )
    name: Mapped[str] = mapped_column(String, nullable=False)
    type: Mapped[int] = mapped_column(SmallInteger, nullable=False)


class SwitchbotReading(Base):
    __tablename__ = "switchbot_readings"
    __table_args__ = (
        UniqueConstraint("mac", "timestamp", name="switchbot_readings_mac_timestamp_uniq"),
        Index("switchbot_readings_mac_timestamp_idx", "mac", "timestamp"),
        Index("switchbot_readings_sensor_id_timestamp_idx", "sensor_id", "timestamp"),
    )

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    sensor_id: Mapped[UUID] = mapped_column(
        PostgresUUID(as_uuid=True),
        ForeignKey("sensors.id"),
        nullable=False,
    )
    mac: Mapped[str] = mapped_column(String, ForeignKey("sensors.mac"), nullable=False)
    timestamp: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    temperature_c: Mapped[float] = mapped_column(Float, nullable=False)
    humidity_pct: Mapped[float] = mapped_column(Float, nullable=False)


class XiaomiReading(Base):
    __tablename__ = "xiaomi_readings"
    __table_args__ = (
        UniqueConstraint("mac", "timestamp", name="xiaomi_readings_mac_timestamp_uniq"),
        Index("xiaomi_readings_mac_timestamp_idx", "mac", "timestamp"),
        Index("xiaomi_readings_sensor_id_timestamp_idx", "sensor_id", "timestamp"),
    )

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    sensor_id: Mapped[UUID] = mapped_column(
        PostgresUUID(as_uuid=True),
        ForeignKey("sensors.id"),
        nullable=False,
    )
    mac: Mapped[str] = mapped_column(String, ForeignKey("sensors.mac"), nullable=False)
    timestamp: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    temperature_c: Mapped[Optional[float]] = mapped_column(Float, nullable=True)
    moisture_pct: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    light_lux: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
    conductivity_us_cm: Mapped[Optional[int]] = mapped_column(Integer, nullable=True)
