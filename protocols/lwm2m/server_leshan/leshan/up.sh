#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
echo "[leshan] Starting stack..."
docker compose up -d
echo "[leshan] Done. UI: http://localhost:8081"
