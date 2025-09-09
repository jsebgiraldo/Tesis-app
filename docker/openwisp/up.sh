#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
echo "[openwisp] Starting stack..."
docker compose --project-name openwisp up -d
echo "[openwisp] Done. Dashboard: http://localhost:8091  API: http://localhost:8092"
