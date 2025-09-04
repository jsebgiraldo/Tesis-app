#!/usr/bin/env bash
set -euo pipefail

# Run ThingsBoard DB install/migrations and optionally load demo data.
# Usage:
#   ./install.sh           # install without demo data
#   LOAD_DEMO=true ./install.sh  # install and load demo data

cd "$(dirname "$0")"

LOAD_DEMO_FLAG=${LOAD_DEMO:-false}

echo "[thingsboard] Running install (LOAD_DEMO=${LOAD_DEMO_FLAG}) ..."
docker compose run --rm \
  -e INSTALL_TB=true \
  -e LOAD_DEMO=${LOAD_DEMO_FLAG} \
  thingsboard-ce
echo "[thingsboard] Install finished. You can now start the stack with ./up.sh if not running."
