#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
cd "$SCRIPT_DIR"

if [ ! -f .env ]; then
  echo "[tb-edge] Falta .env. Copiando .env.example..."
  cp .env.example .env
fi

# Validación de variables requeridas
set +e
source .env 2>/dev/null
set -e

if [ -z "${CLOUD_RPC_HOST:-}" ] || [ -z "${CLOUD_ROUTING_KEY:-}" ] || [ -z "${CLOUD_ROUTING_SECRET:-}" ]; then
  echo "[tb-edge] Debes configurar CLOUD_RPC_HOST, CLOUD_ROUTING_KEY y CLOUD_ROUTING_SECRET en .env"
  exit 1
fi

echo "[tb-edge] Pull de imágenes..."
docker compose pull

echo "[tb-edge] Arrancando por primera vez..."
docker compose up -d

echo "Abre http://localhost:18080 y entra con tus credenciales de tenant del servidor."
