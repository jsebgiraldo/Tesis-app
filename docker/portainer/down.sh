#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
echo "[portainer] Stopping Portainer..."
docker compose down
echo "[portainer] Done."
