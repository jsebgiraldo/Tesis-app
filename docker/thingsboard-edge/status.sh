#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
cd "$SCRIPT_DIR"

URL=${1:-http://thingsboard.edge.play:8080}
for i in {1..60}; do
  if curl -fsS "$URL" >/dev/null 2>&1; then
    echo "ThingsBoard Edge está arriba en $URL"; exit 0; fi
  echo "Esperando ThingsBoard Edge... ($i)"; sleep 2
done

echo "Timeout esperando ThingsBoard Edge"
exit 1
