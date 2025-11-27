#!/bin/bash
# ESP32 Firmware Upload Script for macOS/Linux

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
CHIP="esp32"
BAUD_RATE="460800"
FIRMWARE_FILE="firmware.bin"
FLASH_OFFSET="0x10000"

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

# Check if esptool is available, install if not
if ! python3 -m esptool version &> /dev/null; then
    echo -e "${YELLOW}esptool not found. Installing...${NC}"
    python3 -m pip install --user esptool
fi

# Upload firmware
echo -e "${GREEN}Uploading firmware to ESP32...${NC}"
echo "Port: $PORT"
echo "Firmware: $FIRMWARE_FILE"
echo "Baud rate: $BAUD_RATE"
echo ""

python3 -m esptool --chip "$CHIP" --port "$PORT" --baud "$BAUD_RATE" \
    write_flash "$FLASH_OFFSET" "$FIRMWARE_FILE"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}Upload successful!${NC}"
else
    echo -e "${RED}Upload failed!${NC}"
    echo ""
    echo "Troubleshooting:"
    echo "  1. Make sure the ESP32 is connected"
    echo "  2. Try holding the BOOT button during upload"
    echo "  3. Check if you have permission to access the serial port"
    echo "     Run: sudo $0 $PORT"
    exit 1
fi
