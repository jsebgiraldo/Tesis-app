#!/usr/bin/env zsh
# Captura de tráfico con tshark
# Uso:
#  sudo ./tools/tshark_capture.sh -i en0 -f "host 192.168.1.50" -o data/pcaps/mqtt_test
# Genera: data/pcaps/mqtt_test_YYYYMMDD_HHMMSS.pcapng

set -euo pipefail

IFACE=""
FILTER=""
OUT_PREFIX="data/pcaps/capture"

while getopts "i:f:o:h" opt; do
  case $opt in
    i) IFACE="$OPTARG" ;;
    f) FILTER="$OPTARG" ;;
    o) OUT_PREFIX="$OPTARG" ;;
    h)
      echo "Uso: sudo $0 -i <iface> [-f <filtro_bpf>] [-o <prefijo_salida>]";
      exit 0 ;;
    *) echo "Parámetros inválidos"; exit 1 ;;
  esac
done

if [[ -z "$IFACE" ]]; then
  echo "Debes especificar interfaz con -i (ej: en0)" >&2
  exit 1
fi

TS=$(date +%Y%m%d_%H%M%S)
OUT_DIR=$(dirname "$OUT_PREFIX")
mkdir -p "$OUT_DIR"
OUT_FILE="${OUT_PREFIX}_${TS}.pcapng"

echo "Interfaz: $IFACE"
[[ -n "$FILTER" ]] && echo "Filtro: $FILTER"
echo "Salida:  $OUT_FILE"

tshark -i "$IFACE" ${FILTER:+-f "$FILTER"} -w "$OUT_FILE" --time-stamp-format=iso

echo "Captura finalizada: $OUT_FILE"
