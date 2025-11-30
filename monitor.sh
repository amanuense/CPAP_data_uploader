#!/bin/bash
# Monitor serial output from ESP32

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

# Monitor device
echo "Starting serial monitor..."
# Use full path to pio to work with sudo
PIO_PATH=$(which pio)
sudo -E "$PIO_PATH" device monitor -e pico32
