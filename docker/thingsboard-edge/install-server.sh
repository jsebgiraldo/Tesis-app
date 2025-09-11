#!/usr/bin/env bash
set -euo pipefail
# Auto-elevate if not root for docker access
if [ "${EUID:-$(id -u)}" -ne 0 ]; then
  exec sudo -E "$0" "$@"
fi
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
cd "$SCRIPT_DIR"

# Inicializa/actualiza la base de datos del ThingsBoard Server CE local
# Uso:
#   ./install-server.sh                 # solo esquema
#   LOAD_DEMO=true ./install-server.sh  # esquema + datos demo

LOAD_DEMO_FLAG=${LOAD_DEMO:-false}

docker compose pull tb-server || true

echo "[tb-server] Ejecutando instalación DB (LOAD_DEMO=${LOAD_DEMO_FLAG}) ..."
docker compose run --rm \
  -e INSTALL_TB=true \
  -e LOAD_DEMO=${LOAD_DEMO_FLAG} \
  tb-server

echo "[tb-server] Instalación finalizada. Abre http://localhost:8081"
