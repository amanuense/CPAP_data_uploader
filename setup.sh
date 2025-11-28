#!/bin/bash
# Quick setup script for new project checkout
# Sets up Python venv, PlatformIO, and libsmb2 component

set -e

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}=== Project Setup ===${NC}"
echo ""

# 1. Create Python virtual environment
if [ ! -d "venv" ]; then
    echo -e "${YELLOW}Creating Python virtual environment...${NC}"
    python3 -m venv venv
    echo -e "${GREEN}✓ Virtual environment created${NC}"
else
    echo -e "${GREEN}✓ Virtual environment already exists${NC}"
fi

# 2. Install PlatformIO
echo -e "${YELLOW}Installing PlatformIO...${NC}"
source venv/bin/activate
pip install --upgrade pip --quiet
pip install platformio --quiet
echo -e "${GREEN}✓ PlatformIO installed${NC}"

# 3. Setup libsmb2 (if SMB upload is enabled)
if grep -q "ENABLE_SMB_UPLOAD" platformio.ini; then
    echo -e "${YELLOW}Setting up libsmb2 component...${NC}"
    ./scripts/setup_libsmb2.sh
else
    echo -e "${YELLOW}ℹ SMB upload not enabled, skipping libsmb2${NC}"
fi

# 4. Install PlatformIO dependencies
echo -e "${YELLOW}Installing PlatformIO dependencies...${NC}"
pio pkg install
echo -e "${GREEN}✓ Dependencies installed${NC}"

echo ""
echo -e "${GREEN}=== Setup Complete! ===${NC}"
echo ""
echo "Quick commands:"
echo "  Build & Upload:  ./build_upload.sh"
echo "  Monitor:         ./monitor.sh"
echo "  Run Tests:       source venv/bin/activate && pio test -e native"
echo ""
