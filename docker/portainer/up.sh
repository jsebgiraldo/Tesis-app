#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
echo "[portainer] Starting Portainer..."
docker compose up -d
echo "[portainer] Ready. UI: http://localhost:9000  (Primera vez: crea usuario admin)"
