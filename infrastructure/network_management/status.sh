#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
echo "[openwisp] Status:"
docker compose --project-name openwisp ps
