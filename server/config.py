import json
import os
from pathlib import Path


def load_config() -> dict:
    env = os.getenv("ENV", "dev")
    path = Path("config") / f"{env}.json"

    with path.open(encoding="utf-8") as f:
        return json.load(f)
