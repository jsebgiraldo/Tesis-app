#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
echo "[leshan] Stopping stack..."
docker compose down
echo "[leshan] Done."
