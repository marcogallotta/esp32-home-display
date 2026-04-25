"""Add sensor ids

Revision ID: 9287dc70b9af
Revises: 9065001b9646
Create Date: 2026-04-24 20:59:40.847246
"""

from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects import postgresql


revision: str = "9287dc70b9af"
down_revision: Union[str, Sequence[str], None] = "9065001b9646"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.execute('CREATE EXTENSION IF NOT EXISTS "pgcrypto"')

    op.add_column(
        "sensors",
        sa.Column(
            "id",
            postgresql.UUID(as_uuid=True),
            server_default=sa.text("gen_random_uuid()"),
            nullable=False,
        ),
    )

    op.create_unique_constraint("sensors_id_key", "sensors", ["id"])

    op.add_column(
        "switchbot_readings",
        sa.Column("sensor_id", postgresql.UUID(as_uuid=True), nullable=True),
    )
    op.add_column(
        "xiaomi_readings",
        sa.Column("sensor_id", postgresql.UUID(as_uuid=True), nullable=True),
    )

    op.execute("""
        UPDATE switchbot_readings r
        SET sensor_id = s.id
        FROM sensors s
        WHERE r.mac = s.mac
    """)

    op.execute("""
        UPDATE xiaomi_readings r
        SET sensor_id = s.id
        FROM sensors s
        WHERE r.mac = s.mac
    """)

    op.alter_column("switchbot_readings", "sensor_id", nullable=False)
    op.alter_column("xiaomi_readings", "sensor_id", nullable=False)

    op.create_foreign_key(
        "switchbot_readings_sensor_id_fkey",
        "switchbot_readings",
        "sensors",
        ["sensor_id"],
        ["id"],
    )
    op.create_foreign_key(
        "xiaomi_readings_sensor_id_fkey",
        "xiaomi_readings",
        "sensors",
        ["sensor_id"],
        ["id"],
    )

    op.create_index(
        "switchbot_readings_sensor_id_timestamp_idx",
        "switchbot_readings",
        ["sensor_id", "timestamp"],
    )
    op.create_index(
        "xiaomi_readings_sensor_id_timestamp_idx",
        "xiaomi_readings",
        ["sensor_id", "timestamp"],
    )


def downgrade() -> None:
    op.drop_index("xiaomi_readings_sensor_id_timestamp_idx", table_name="xiaomi_readings")
    op.drop_index("switchbot_readings_sensor_id_timestamp_idx", table_name="switchbot_readings")

    op.drop_constraint("xiaomi_readings_sensor_id_fkey", "xiaomi_readings", type_="foreignkey")
    op.drop_constraint("switchbot_readings_sensor_id_fkey", "switchbot_readings", type_="foreignkey")

    op.drop_column("xiaomi_readings", "sensor_id")
    op.drop_column("switchbot_readings", "sensor_id")

    op.drop_constraint("sensors_id_key", "sensors", type_="unique")
    op.drop_column("sensors", "id")
