#!/bin/bash
# ESP32 Firmware Upload Script for macOS/Linux

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Configuration
CHIP="esp32"
BAUD_RATE="460800"
FIRMWARE_FILE="$SCRIPT_DIR/firmware.bin"
FLASH_OFFSET="0x10000"
VENV_DIR="$SCRIPT_DIR/.venv"

# Check if port is provided
if [ -z "$1" ]; then
    echo -e "${RED}Error: Serial port not specified${NC}"
    echo "Usage: $0 <serial_port>"
    echo ""
    echo "Examples:"
    echo "  macOS:  $0 /dev/cu.usbserial-0001"
    echo "  Linux:  $0 /dev/ttyUSB0"
    echo ""
    echo "Available ports:"
    if [[ "$OSTYPE" == "darwin"* ]]; then
        ls /dev/cu.* 2>/dev/null || echo "  No USB serial devices found"
    else
        ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || echo "  No USB serial devices found"
    fi
    exit 1
fi

PORT="$1"

# Check if firmware file exists
if [ ! -f "$FIRMWARE_FILE" ]; then
    echo -e "${RED}Error: Firmware file '$FIRMWARE_FILE' not found${NC}"
    exit 1
fi

# Check if Python is available
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}Error: Python 3 is not installed${NC}"
    echo "Please install Python 3.7 or later"
    exit 1
fi

# Setup virtual environment if it doesn't exist
if [ ! -d "$VENV_DIR" ]; then
    echo -e "${YELLOW}Creating virtual environment...${NC}"
    python3 -m venv "$VENV_DIR"
fi

# Activate virtual environment
source "$VENV_DIR/bin/activate"

# Check if esptool is available in venv, install if not
if ! python -m esptool version &> /dev/null; then
    echo -e "${YELLOW}Installing esptool...${NC}"
    pip install esptool
fi

# Upload firmware
echo -e "${GREEN}Uploading firmware to ESP32...${NC}"
echo "Port: $PORT"
echo "Firmware: $FIRMWARE_FILE"
echo "Baud rate: $BAUD_RATE"
echo ""

python -m esptool --chip "$CHIP" --port "$PORT" --baud "$BAUD_RATE" \
    write_flash "$FLASH_OFFSET" "$FIRMWARE_FILE"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}Upload successful!${NC}"
    deactivate
else
    echo -e "${RED}Upload failed!${NC}"
    echo ""
    echo "Troubleshooting:"
    echo "  1. Make sure the ESP32 is connected"
    echo "  2. Try holding the BOOT button during upload"
    echo "  3. Check if you have permission to access the serial port"
    echo "     Run: sudo $0 $PORT"
    deactivate
    exit 1
fi
