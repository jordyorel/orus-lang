#!/bin/bash

# Orus Playground Launcher
# Cross-platform script to start the playground server

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PLAYGROUND_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}🚀 Orus Playground Launcher${NC}"
echo "========================================="

# Check if Orus binary exists
ORUS_BINARY="$PROJECT_ROOT/orus"
if [ ! -f "$ORUS_BINARY" ] || [ ! -x "$ORUS_BINARY" ]; then
    echo -e "${YELLOW}⚠️  Orus binary not found at: $ORUS_BINARY${NC}"
    echo -e "${YELLOW}   Building Orus first...${NC}"
    
    cd "$PROJECT_ROOT"
    if ! make; then
        echo -e "${RED}❌ Failed to build Orus${NC}"
        exit 1
    fi
    
    if [ ! -f "$ORUS_BINARY" ] || [ ! -x "$ORUS_BINARY" ]; then
        echo -e "${RED}❌ Orus binary still not found after build${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}✅ Orus built successfully${NC}"
fi

# Check Python version
PYTHON_CMD=""
if command -v python3 >/dev/null 2>&1; then
    PYTHON_CMD="python3"
elif command -v python >/dev/null 2>&1; then
    PYTHON_VERSION=$(python --version 2>&1 | cut -d' ' -f2 | cut -d'.' -f1)
    if [ "$PYTHON_VERSION" = "3" ]; then
        PYTHON_CMD="python"
    fi
fi

if [ -z "$PYTHON_CMD" ]; then
    echo -e "${RED}❌ Python 3 is required but not found${NC}"
    echo "   Please install Python 3 and try again"
    exit 1
fi

echo -e "${GREEN}✅ Using Python: $PYTHON_CMD${NC}"
echo -e "${GREEN}✅ Orus binary: $ORUS_BINARY${NC}"

# Default port
PORT=${1:-8000}

# Start the server
echo -e "${BLUE}🌐 Starting playground server on port $PORT...${NC}"
echo -e "${BLUE}   Open your browser to: http://localhost:$PORT${NC}"
echo -e "${BLUE}   Press Ctrl+C to stop the server${NC}"
echo ""

cd "$PLAYGROUND_DIR"
exec "$PYTHON_CMD" "$SCRIPT_DIR/server.py" "$PORT"