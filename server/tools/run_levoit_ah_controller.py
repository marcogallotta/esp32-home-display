#!/usr/bin/env python3
import argparse
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))

from app.config import load_config
from app.levoit_runner import run_once


def main():
    parser = argparse.ArgumentParser(description="Levoit AH controller")
    parser.add_argument("--once", action="store_true",
                        help="Run once and exit (default: loop)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Compute and print decision but send no commands to the device")
    parser.add_argument("--interval", type=int,
                        help="Override poll interval in seconds")
    args = parser.parse_args()

    config = load_config()

    if args.once:
        code, message = run_once(config, dry_run=args.dry_run)
        print(message)
        sys.exit(code)

    interval = args.interval if args.interval is not None else config.levoit_ah_controller.poll_interval_seconds
    while True:
        print(datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"))
        _, message = run_once(config, dry_run=args.dry_run)
        print(message)
        time.sleep(interval)


if __name__ == "__main__":
    main()
