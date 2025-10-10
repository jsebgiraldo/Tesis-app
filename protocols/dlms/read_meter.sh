#!/bin/bash
# Script de ejemplo para leer voltaje y corriente del medidor Microstar del laboratorio
#
# Configuración del medidor:
# - IP: 192.168.5.177
# - Puerto: 3333
# - SAP: 1
# - Password: 22222222
# - Server logical: 0 (crítico para este modelo)
# - Server physical: 1

python3 dlms_reader.py \
  --host 192.168.5.177 \
  --port 3333 \
  --client-sap 1 \
  --server-logical 0 \
  --server-physical 1 \
  --password 22222222 \
  --measurement voltage_l1 current_l1 \
  "$@"

# Uso:
#   ./read_meter.sh           # Lectura normal
#   ./read_meter.sh --verbose # Lectura con debug
