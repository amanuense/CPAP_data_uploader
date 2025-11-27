# ESP32 Firmware Upload Tool

This package contains the minimal tools needed to upload precompiled firmware to ESP32 devices.

## Requirements

### Windows
- USB drivers for ESP32 (CH340/CP210x depending on your board)
- No Python required - esptool.exe is included

### macOS/Linux
- Python 3.7 or later
- USB drivers for ESP32 (CH340/CP210x depending on your board)

## Quick Start

### Windows
```cmd
upload.bat COM3
```

### macOS/Linux
```bash
./upload.sh /dev/ttyUSB0
```

Replace `COM3` or `/dev/ttyUSB0` with your actual serial port.

## Finding Your Serial Port

### Windows
- Open Device Manager
- Look under "Ports (COM & LPT)"
- Find "USB-SERIAL CH340" or similar (e.g., COM3, COM4)

### macOS
```bash
ls /dev/cu.*
```
Look for `/dev/cu.usbserial-*` or `/dev/cu.SLAB_USBtoUART`

### Linux
```bash
ls /dev/ttyUSB* /dev/ttyACM*
```
Usually `/dev/ttyUSB0` or `/dev/ttyACM0`

## Manual Installation

### Windows
If the script doesn't work, run esptool.exe directly:
```cmd
esptool.exe --chip esp32 --port COM3 --baud 460800 write_flash 0x10000 firmware.bin
```

### macOS/Linux
If the automatic script doesn't work, install esptool manually:
```bash
pip install esptool
```

Then run:
```bash
esptool.py --chip esp32 --port YOUR_PORT --baud 460800 write_flash 0x10000 firmware.bin
```

## Troubleshooting

- **Permission denied (Linux/Mac)**: Run `sudo ./upload.sh /dev/ttyUSB0` or add your user to the dialout group
- **Port not found**: Make sure the ESP32 is connected and drivers are installed
- **Upload fails**: Try holding the BOOT button on the ESP32 while uploading
