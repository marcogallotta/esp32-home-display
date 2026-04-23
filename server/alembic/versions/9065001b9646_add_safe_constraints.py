"""Add constraints to db

Revision ID: 9065001b9646
Revises:
Create Date: 2026-04-22 20:58:54.180470

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


# revision identifiers, used by Alembic.
revision: str = "9065001b9646"
down_revision: Union[str, Sequence[str], None] = None
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    """Upgrade schema."""
    op.alter_column(
        "sensors",
        "type",
        existing_type=sa.SmallInteger(),
        nullable=False,
    )
    op.create_check_constraint(
        "sensors_type_valid",
        "sensors",
        "type IN (1, 2)",
    )


def downgrade() -> None:
    """Downgrade schema."""
    op.drop_constraint("sensors_type_valid", "sensors", type_="check")
    op.alter_column(
        "sensors",
        "type",
        existing_type=sa.SmallInteger(),
        nullable=True,
    )
