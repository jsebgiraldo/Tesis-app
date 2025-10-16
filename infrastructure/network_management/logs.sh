#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
echo "[openwisp] Tailing logs (Ctrl-C to stop)..."
docker compose --project-name openwisp logs -f --tail=100
