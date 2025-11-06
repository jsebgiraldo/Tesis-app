#!/bin/bash
# DLMS Multi-Meter Admin System Launcher
# Starts API server and Dashboard

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BLUE}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  DLMS Multi-Meter Admin System                      ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════════════════════════╝${NC}"
echo ""

# Check if virtual environment exists
if [ ! -d "venv" ]; then
    echo -e "${YELLOW}Creating virtual environment...${NC}"
    python3 -m venv venv
fi

# Activate virtual environment
source venv/bin/activate

# Install/upgrade dependencies
echo -e "${BLUE}Checking dependencies...${NC}"
pip install -q --upgrade pip
pip install -q -r requirements-admin.txt

# Create data directory if it doesn't exist
mkdir -p data

# Initialize database
echo -e "${BLUE}Initializing database...${NC}"
python3 -c "
from admin.database import init_db
db = init_db('data/admin.db')
print('✓ Database initialized')
"

echo ""
echo -e "${GREEN}✓ Setup complete!${NC}"
echo ""
echo -e "${BLUE}Starting services...${NC}"
echo ""

# Function to cleanup on exit
cleanup() {
    echo ""
    echo -e "${YELLOW}Shutting down services...${NC}"
    kill $API_PID 2>/dev/null || true
    kill $DASHBOARD_PID 2>/dev/null || true
    exit 0
}

trap cleanup SIGINT SIGTERM

# Start API server in background
echo -e "${BLUE}[1/2] Starting API server on http://localhost:8000${NC}"
python3 -m uvicorn admin.api:app --host 0.0.0.0 --port 8000 --log-level warning &
API_PID=$!

# Wait for API to be ready
sleep 3

# Start dashboard in background
echo -e "${BLUE}[2/2] Starting dashboard on http://localhost:8501${NC}"
streamlit run admin/dashboard.py --server.port 8501 --server.headless true &
DASHBOARD_PID=$!

echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║  🚀 System Running!                                  ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "  📊 Dashboard:  ${BLUE}http://localhost:8501${NC}"
echo -e "  🔌 API:        ${BLUE}http://localhost:8000${NC}"
echo -e "  📚 API Docs:   ${BLUE}http://localhost:8000/docs${NC}"
echo ""
echo -e "${YELLOW}Press Ctrl+C to stop all services${NC}"
echo ""

# Wait for processes
wait
