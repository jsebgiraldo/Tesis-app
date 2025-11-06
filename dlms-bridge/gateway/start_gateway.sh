#!/bin/bash
# ==============================================================================
# ThingsBoard Gateway - Quick Start Script
# ==============================================================================
# Script simplificado para desarrollo y pruebas sin instalar systemd
#
# Uso:
#   ./start_gateway.sh
#
# ==============================================================================

set -e

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARENT_DIR="$(dirname "$PROJECT_DIR")"

echo -e "${BLUE}===================================================================${NC}"
echo -e "${BLUE}ThingsBoard Gateway - DLMS Connector (Development Mode)${NC}"
echo -e "${BLUE}===================================================================${NC}"

# Check if virtual environment exists
if [ ! -d "${PARENT_DIR}/venv" ]; then
    echo -e "${YELLOW}⚠ Virtual environment not found. Creating...${NC}"
    python3 -m venv "${PARENT_DIR}/venv"
fi

# Activate virtual environment
source "${PARENT_DIR}/venv/bin/activate"

# Install dependencies
echo -e "${BLUE}Installing dependencies...${NC}"
pip install -q thingsboard-gateway

# Check configuration
if [ ! -f "${PROJECT_DIR}/config/tb_gateway.yaml" ]; then
    echo -e "${YELLOW}⚠ Configuration file not found: config/tb_gateway.yaml${NC}"
    exit 1
fi

# Export PYTHONPATH to include connectors and parent directory
export PYTHONPATH="${PROJECT_DIR}/connectors:${PARENT_DIR}:${PYTHONPATH}"

echo -e "${GREEN}✓ Environment ready${NC}"
echo -e "${BLUE}Starting ThingsBoard Gateway...${NC}"
echo ""

# Start gateway
thingsboard-gateway --config "${PROJECT_DIR}/config/tb_gateway.yaml"
