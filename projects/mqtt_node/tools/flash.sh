#!/usr/bin/env zsh
set -euo pipefail
PORT=${1:-/dev/tty.usbserial-0001}
BAUD=${BAUD:-460800}

idf.py -p "$PORT" -b "$BAUD" flash monitor
