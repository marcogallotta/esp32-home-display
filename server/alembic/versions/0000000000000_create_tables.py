"""Create initial tables

Revision ID: 0000000000000
Revises:
Create Date: 2026-05-13 00:00:00.000000

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


revision: str = "0000000000000"
down_revision: Union[str, Sequence[str], None] = None
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.create_table(
        "sensors",
        sa.Column("mac", sa.String(), nullable=False),
        sa.Column("name", sa.String(), nullable=False),
        sa.Column("type", sa.SmallInteger(), nullable=True),
        sa.PrimaryKeyConstraint("mac"),
    )
    op.create_table(
        "switchbot_readings",
        sa.Column("id", sa.Integer(), nullable=False),
        sa.Column("mac", sa.String(), nullable=False),
        sa.Column("timestamp", sa.DateTime(timezone=True), nullable=False),
        sa.Column("temperature_c", sa.Float(), nullable=False),
        sa.Column("humidity_pct", sa.Float(), nullable=False),
        sa.ForeignKeyConstraint(["mac"], ["sensors.mac"]),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("mac", "timestamp", name="switchbot_readings_mac_timestamp_uniq"),
    )
    op.create_index("switchbot_readings_mac_timestamp_idx", "switchbot_readings", ["mac", "timestamp"])
    op.create_table(
        "xiaomi_readings",
        sa.Column("id", sa.Integer(), nullable=False),
        sa.Column("mac", sa.String(), nullable=False),
        sa.Column("timestamp", sa.DateTime(timezone=True), nullable=False),
        sa.Column("temperature_c", sa.Float(), nullable=True),
        sa.Column("moisture_pct", sa.Integer(), nullable=True),
        sa.Column("light_lux", sa.Integer(), nullable=True),
        sa.Column("conductivity_us_cm", sa.Integer(), nullable=True),
        sa.ForeignKeyConstraint(["mac"], ["sensors.mac"]),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("mac", "timestamp", name="xiaomi_readings_mac_timestamp_uniq"),
    )
    op.create_index("xiaomi_readings_mac_timestamp_idx", "xiaomi_readings", ["mac", "timestamp"])


def downgrade() -> None:
    op.drop_index("xiaomi_readings_mac_timestamp_idx", "xiaomi_readings")
    op.drop_table("xiaomi_readings")
    op.drop_index("switchbot_readings_mac_timestamp_idx", "switchbot_readings")
    op.drop_table("switchbot_readings")
    op.drop_table("sensors")
