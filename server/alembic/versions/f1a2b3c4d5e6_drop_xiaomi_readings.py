"""Drop xiaomi_readings

Flower Care (Xiaomi) plant data is now owned by the external plant-monitor
service (read over an active BLE GATT connection) and served to the dashboard
via app/plant_proxy.py. The firmware no longer scans Xiaomi and the server no
longer ingests or stores it, so the readings table is removed. The Xiaomi
sensor row in `sensors` (type 2) is kept as a plant-sensor identity marker.

The previously stored readings were the unreliable passive-scan data (stale
replayed values) and are intentionally not preserved.

Revision ID: f1a2b3c4d5e6
Revises: 9287dc70b9af
Create Date: 2026-06-11 00:00:00.000000
"""

from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects import postgresql


revision: str = "f1a2b3c4d5e6"
down_revision: Union[str, Sequence[str], None] = "9287dc70b9af"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.drop_index("xiaomi_readings_sensor_id_timestamp_idx", table_name="xiaomi_readings")
    op.drop_index("xiaomi_readings_mac_timestamp_idx", table_name="xiaomi_readings")
    op.drop_table("xiaomi_readings")


def downgrade() -> None:
    # Recreates the table schema as it existed at head. Data is not restored.
    op.create_table(
        "xiaomi_readings",
        sa.Column("id", sa.Integer(), nullable=False),
        sa.Column("sensor_id", postgresql.UUID(as_uuid=True), nullable=False),
        sa.Column("mac", sa.String(), nullable=False),
        sa.Column("timestamp", sa.DateTime(timezone=True), nullable=False),
        sa.Column("temperature_c", sa.Float(), nullable=True),
        sa.Column("moisture_pct", sa.Integer(), nullable=True),
        sa.Column("light_lux", sa.Integer(), nullable=True),
        sa.Column("conductivity_us_cm", sa.Integer(), nullable=True),
        sa.ForeignKeyConstraint(["mac"], ["sensors.mac"]),
        sa.ForeignKeyConstraint(["sensor_id"], ["sensors.id"], name="xiaomi_readings_sensor_id_fkey"),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("mac", "timestamp", name="xiaomi_readings_mac_timestamp_uniq"),
    )
    op.create_index("xiaomi_readings_mac_timestamp_idx", "xiaomi_readings", ["mac", "timestamp"])
    op.create_index(
        "xiaomi_readings_sensor_id_timestamp_idx", "xiaomi_readings", ["sensor_id", "timestamp"]
    )
