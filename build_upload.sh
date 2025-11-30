#!/bin/bash
# Build and upload firmware to ESP32

set -e  # Exit on error

# Check if venv exists
if [ ! -d "venv" ]; then
    echo "Error: Virtual environment not found."
    echo "Please run ./setup.sh first to set up the development environment."
    exit 1
fi

# Activate virtual environment
source venv/bin/activate

# Check if pio is available
if ! command -v pio &> /dev/null; then
    echo "Error: PlatformIO not found in virtual environment."
    echo "Please run ./setup.sh to install dependencies."
    exit 1
fi

# Build and upload
echo "Building and uploading firmware..."
# Use full path to pio to work with sudo
PIO_PATH=$(which pio)
sudo -E "$PIO_PATH" run -e pico32 -t upload
