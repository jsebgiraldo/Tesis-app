#!/usr/bin/env bash
set -euo pipefail
# Helper to run Docker with proper mounts for mqtt_node
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
REPO_DIR=$(cd "$SCRIPT_DIR/.." && pwd)

IMAGE=${1:-esp-idf-iot:latest}

docker run --rm -it \
  -v "$REPO_DIR/projects/mqtt_node":/project \
  -v "$REPO_DIR/projects/common/components":/project/components \
  --name idf-mqtt "$IMAGE"
