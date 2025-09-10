#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
cd "$SCRIPT_DIR"

read -rp "Esto detendrá tb-edge y borrará volúmenes (datos). ¿Continuar? (yes/NO): " ans
if [[ "${ans:-}" != "yes" ]]; then
  echo "Cancelado."; exit 1; fi

docker compose down -v || true

echo "Hecho. Puedes recrear el stack con ./up.sh"
