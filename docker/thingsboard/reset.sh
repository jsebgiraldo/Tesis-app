#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
REPO_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)

read -rp "Esto detendrá TB y borrará los volúmenes de Docker (Postgres/Kafka). ¿Continuar? (yes/NO): " ans
if [[ "${ans:-}" != "yes" ]]; then
  echo "Cancelado."
  exit 1
fi

cd "$SCRIPT_DIR"
docker compose down -v || true
echo "Hecho. Vuelve a ejecutar ./up.sh y luego la inicialización:"
echo "  docker compose run --rm -e INSTALL_TB=true -e LOAD_DEMO=true thingsboard-ce"

