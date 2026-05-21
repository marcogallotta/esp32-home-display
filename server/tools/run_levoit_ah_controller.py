#!/usr/bin/env python3
"""Dry-run one-shot Levoit AH controller. No humidifier command is sent."""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))

from app.config import load_config
from app.levoit_runner import run_once


def main():
    parser = argparse.ArgumentParser(description="Levoit AH controller dry-run")
    parser.add_argument("--once", action="store_true", required=True,
                        help="Run one-shot (required; loop not implemented yet)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Compute and print decision but send no commands to the device")
    args = parser.parse_args()

    config = load_config()
    code, message = run_once(config, dry_run=args.dry_run)
    print(message)
    sys.exit(code)


if __name__ == "__main__":
    main()
