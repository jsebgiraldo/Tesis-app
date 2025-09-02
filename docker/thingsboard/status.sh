#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
cd "$SCRIPT_DIR"

# Wait for ThingsBoard to respond on 8080
for i in {1..60}; do
  if curl -fsS http://localhost:8080 >/dev/null 2>&1; then
    echo "ThingsBoard is up at http://localhost:8080"
    exit 0
  fi
  echo "Waiting for ThingsBoard... ($i)"
  sleep 2

done

echo "Timeout waiting for ThingsBoard"
exit 1
