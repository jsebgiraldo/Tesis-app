#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
REPO_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)

# Ensure data dirs exist with permissive perms for Docker on macOS/Linux
mkdir -p "$REPO_DIR/data/thingsboard/postgres" "$REPO_DIR/data/thingsboard/logs"

echo "Starting ThingsBoard CE stack (Postgres + Kafka + tb-node) ..."
cd "$SCRIPT_DIR"
docker compose up -d
echo "REST/API (and UI if present): http://localhost:8080"
echo "Kafka (host): localhost:9092"

