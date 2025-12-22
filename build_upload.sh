#!/bin/bash
# Build and upload firmware to ESP32

set -e  # Exit on error

# Function to show usage
show_usage() {
    echo "Usage: $0 [build|upload|both] [firmware_type] [port]"
    echo ""
    echo "Commands:"
    echo "  build         - Build firmware only (no sudo required)"
    echo "  upload        - Upload firmware only (requires sudo)"
    echo "  both          - Build and upload (default, requires sudo for upload)"
    echo ""
    echo "Firmware Types (REQUIRED):"
    echo "  pico32        - Standard firmware (3MB partition, no OTA)"
    echo "  pico32-ota    - OTA firmware (1.5MB partitions, web updates)"
    echo ""
    echo "Options:"
    echo "  port          - Serial port (e.g., /dev/ttyUSB0, /dev/ttyACM0)"
    echo "                  If not specified, PlatformIO will auto-detect"
    echo ""
    echo "Examples:"
    echo "  $0 build pico32-ota                    # Build OTA firmware only"
    echo "  $0 build pico32                        # Build standard firmware only"
    echo "  $0 upload pico32-ota                   # Upload OTA firmware (requires previous build)"
    echo "  $0 upload pico32 /dev/ttyUSB0          # Upload standard firmware to specific port"
    echo "  $0 both pico32-ota                     # Build and upload OTA firmware"
    echo "  $0 both pico32 /dev/ttyUSB0            # Build and upload standard firmware to specific port"
}

# Parse command line arguments
COMMAND=${1:-both}
FIRMWARE_TYPE=$2
PORT=$3

# Check if firmware type is provided
if [ -z "$FIRMWARE_TYPE" ]; then
    echo "Error: Firmware type is required!"
    echo ""
    show_usage
    exit 1
fi

# Validate command
case $COMMAND in
    build|upload|both)
        ;;
    -h|--help|help)
        show_usage
        exit 0
        ;;
    *)
        echo "Error: Invalid command '$COMMAND'"
        echo ""
        show_usage
        exit 1
        ;;
esac

# Validate firmware type
case $FIRMWARE_TYPE in
    pico32|pico32-ota)
        ;;
    *)
        echo "Error: Invalid firmware type '$FIRMWARE_TYPE'"
        echo "Valid options: pico32, pico32-ota"
        echo ""
        show_usage
        exit 1
        ;;
esac

# Set firmware description
case $FIRMWARE_TYPE in
    pico32)
        FIRMWARE_DESC="Standard firmware (3MB partition, no OTA)"
        ;;
    pico32-ota)
        FIRMWARE_DESC="OTA firmware (1.5MB partitions, web updates)"
        ;;
esac

echo "Selected firmware: $FIRMWARE_TYPE - $FIRMWARE_DESC"

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

# Get PlatformIO path for sudo usage
PIO_PATH=$(which pio)

# Build step
if [ "$COMMAND" = "build" ] || [ "$COMMAND" = "both" ]; then
    echo "Building firmware..."
    pio run -e $FIRMWARE_TYPE
    echo "✅ Build completed successfully!"
    echo "Firmware location: .pio/build/$FIRMWARE_TYPE/firmware.bin"
fi

# Upload step
if [ "$COMMAND" = "upload" ] || [ "$COMMAND" = "both" ]; then
    echo "Uploading firmware..."
    
    # Check if firmware exists
    if [ ! -f ".pio/build/$FIRMWARE_TYPE/firmware.bin" ]; then
        echo "Error: Firmware not found. Please build first with:"
        echo "  $0 build $FIRMWARE_TYPE"
        exit 1
    fi
    
    # Prepare upload command
    UPLOAD_CMD="$PIO_PATH run -e $FIRMWARE_TYPE -t upload"
    
    # Add port if specified
    if [ -n "$PORT" ]; then
        UPLOAD_CMD="$UPLOAD_CMD --upload-port $PORT"
        echo "Using port: $PORT"
    else
        echo "Auto-detecting serial port..."
    fi
    
    # Upload with sudo (required for serial port access)
    echo "Note: sudo required for serial port access"
    sudo -E $UPLOAD_CMD
    echo "✅ Upload completed successfully!"
fi

echo ""
echo "Done! 🚀"
