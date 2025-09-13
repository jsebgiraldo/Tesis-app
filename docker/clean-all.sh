#!/usr/bin/env bash
set -euo pipefail

# Limpia todos los stacks Docker Compose bajo este directorio (docker/*)
# Detiene contenedores y borra volúmenes declarados en cada compose.
# Opcionalmente puede ejecutar `docker system prune -a --volumes`.
#
# Uso:
#   bash clean-all.sh [--yes] [--prune-system]
#
# Flags:
#   --yes            No preguntar confirmación
#   --prune-system   Ejecutar también `docker system prune -a --volumes` al final

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
YES=false
PRUNE=false

for arg in "$@"; do
  case "$arg" in
    --yes) YES=true ;;
    --prune-system) PRUNE=true ;;
    -h|--help)
      grep '^#' "$BASH_SOURCE" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *) echo "Flag desconocida: $arg"; exit 1 ;;
  esac
done

# Detectar carpetas con docker-compose.yml (1 o 2 niveles)
STACK_DIRS=()
for f in "$SCRIPT_DIR"/*/docker-compose.yml "$SCRIPT_DIR"/*/*/docker-compose.yml; do
  if [[ -f "$f" ]]; then
    d=$(dirname "$f")
    STACK_DIRS+=("$d")
  fi
done

if [[ ${#STACK_DIRS[@]} -eq 0 ]]; then
  echo "No se encontraron docker-compose.yml bajo $SCRIPT_DIR"
  exit 0
fi

echo "Se limpiarán los siguientes stacks (down -v --remove-orphans):"
for d in "${STACK_DIRS[@]}"; do
  echo " - $d"
done

if [[ "$YES" != true ]]; then
  read -rp $'Esto detendrá contenedores y eliminará volúmenes de estos stacks. ¿Continuar? (yes/NO): ' ans
  if [[ "${ans:-}" != "yes" ]]; then
    echo "Cancelado."; exit 1
  fi
fi

for d in "${STACK_DIRS[@]}"; do
  echo "\n[clean] $d"
  (cd "$d" && docker compose down -v --remove-orphans || true)
done

if [[ "$PRUNE" == true ]]; then
  if [[ "$YES" != true ]]; then
    read -rp $'¿Ejecutar también `docker system prune -a --volumes`? (yes/NO): ' ans2
    if [[ "${ans2:-}" != "yes" ]]; then
      echo "Omitiendo prune del sistema."
      PRUNE=false
    fi
  fi

  if [[ "$PRUNE" == true ]]; then
    echo "\n[clean] Ejecutando docker system prune -a --volumes ..."
    docker system prune -a --volumes -f
  fi
fi

echo "\nLimpieza completada."
