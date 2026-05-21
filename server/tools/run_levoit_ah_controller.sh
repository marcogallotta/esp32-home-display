#!/usr/bin/env bash
set -a
source "$(dirname "$0")/../config/env"
set +a
exec ~/venvs/esp-server/bin/python3 "$(dirname "$0")/run_levoit_ah_controller.py" "$@"
