#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
echo "[openwisp] Stopping stack..."
docker compose --project-name openwisp down
echo "[openwisp] Done."
